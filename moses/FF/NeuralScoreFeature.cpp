#include <vector>
#include <string>
#include <sstream>
#include <deque>
#include <list>
#include <algorithm>

#include <boost/foreach.hpp>

#include "moses/ScoreComponentCollection.h"
#include "moses/TargetPhrase.h"
#include "moses/Hypothesis.h"
#include "moses/FF/NeuralScoreFeature.h"
#include "util/string_piece.hh"

namespace Moses
{

class NeuralScoreState : public FFState
{
public:
  NeuralScoreState(WhichState state, const std::string& lastWord)
  : m_state(state),
    m_lastWord(lastWord) {
    m_lastContext.push_back(m_lastWord);
  }

  NeuralScoreState(WhichState state, const std::vector<std::string>& lastPhrase)
  : m_state(state),
    m_lastWord(lastPhrase.back()) {
    for(size_t i = 0; i < lastPhrase.size(); i++)
      m_lastContext.push_back(lastPhrase[i]);
  }

  int Compare(const FFState& other) const
  {
    const NeuralScoreState &otherState = static_cast<const NeuralScoreState&>(other);
    if(m_lastContext.size() == otherState.m_lastContext.size() &&
        std::equal(m_lastContext.begin(),
                   m_lastContext.end(),
                   otherState.m_lastContext.begin()))
      return 0;
    return (std::lexicographical_compare(m_lastContext.begin(), m_lastContext.end(),
                   otherState.m_lastContext.begin(),
                   otherState.m_lastContext.end())) ? -1 : +1;
  }

  void LimitLength(size_t length) {
    while(m_lastContext.size() > length)
      m_lastContext.pop_front();
  }

  std::string GetLastWord() const {
    return m_lastWord;
  }
  
  WhichState GetState() const {
    return m_state;
  }

private:
  WhichState m_state;
  std::string m_lastWord;
  std::deque<std::string> m_lastContext;
};

const FFState* NeuralScoreFeature::EmptyHypothesisState(const InputType &input) const {
  UTIL_THROW_IF2(input.GetType() != SentenceInput,
                 "This feature function requires the Sentence input type");
  
  const Sentence& sentence = static_cast<const Sentence&>(input);
  
  std::vector<std::string> words;
  for(size_t i = 0; i < sentence.GetSize(); i++)
    words.push_back(sentence.GetWord(i).GetString(0).as_string());
  
  m_nmt->CalcSourceContext(words);
  
  return new NeuralScoreState(WhichState(0, 0), "");
}

NeuralScoreFeature::NeuralScoreFeature(const std::string &line)
  : StatefulFeatureFunction(2, line), m_batchSize(1000), m_stateLength(3), m_factor(0)
{
  ReadParameters();
  m_nmt.reset(new NMT(m_modelPath, m_sourceVocabPath, m_targetVocabPath));
}

void NeuralScoreFeature::ProcessStack(Collector& collector, size_t index) {
  std::map<int, const NeuralScoreState*> states;
  
  bool first = true;
  size_t covered;
  size_t total;
  
  m_pbl.clear();
  
  BOOST_FOREACH(const Hypothesis* h, collector.GetHypotheses()) {
    const Hypothesis& hypothesis = *h;
  
    const FFState* ffstate = hypothesis.GetFFState(index);
    const NeuralScoreState* state
      = static_cast<const NeuralScoreState*>(ffstate);
  
    if(first) {
      const WordsBitmap hypoBitmap = hypothesis.GetWordsBitmap();
      covered = hypoBitmap.GetNumWordsCovered();
      total = hypoBitmap.GetSize();
      first = false;
    }
  
    size_t hypId = hypothesis.GetId();
    states[hypId] = state;
  
    BOOST_FOREACH(const TranslationOptionList* tol, collector.GetOptions(hypId)) {
      TranslationOptionList::const_iterator iter;
      for (iter = tol->begin() ; iter != tol->end() ; ++iter) {
        const TranslationOption& to = **iter;
        const TargetPhrase& tp = to.GetTargetPhrase();
  
        Prefix prefix;
        for(size_t i = 0; i < tp.GetSize(); ++i) {
          prefix.push_back(tp.GetWord(i).GetString(m_factor).as_string());
          if(m_pbl.size() < prefix.size())
            m_pbl.resize(prefix.size());
  
          m_pbl[prefix.size() - 1][prefix][hypId] = Payload();
        }
        if(total - covered == to.GetSize()) {
          prefix.push_back("</s>");
          if(m_pbl.size() < prefix.size())
            m_pbl.resize(prefix.size());
  
          m_pbl[prefix.size() - 1][prefix][hypId] = Payload();
        }
  
      }
    }
  }
  
  std::cerr << "Stack: " << covered << "/" << total << " - ";
  for(size_t l = 0; l < m_pbl.size(); l++) {
    Prefixes& prefixes = m_pbl[l];
  
    std::vector<std::string> allWords;
    std::vector<std::string> allLastWords;
    std::vector<WhichState> allStates;
  
    for(Prefixes::iterator it = prefixes.begin(); it != prefixes.end(); it++) {
      const Prefix& prefix = it->first;
      BOOST_FOREACH(SP& hyp, it->second) {
        size_t hypId = hyp.first;
        allWords.push_back(prefix[l]);
        WhichState state;
        if(prefix.size() == 1) {
          state = states[hypId]->GetState();
          allLastWords.push_back(states[hypId]->GetLastWord());
        }
        else {
          Prefix prevPrefix = prefix;
          prevPrefix.pop_back();
          state = m_pbl[prevPrefix.size() - 1][prevPrefix][hypId].state_;
          allLastWords.push_back(prevPrefix.back());
        }
        allStates.push_back(state);
      }
    }
  
    std::cerr << (l+1) << ":"
      << m_pbl[l].size() << ":" << allStates.size() << " ";
  
    std::vector<double> allProbs;
    std::vector<WhichState> allOutStates;
    std::vector<bool> unks;
    
    m_nmt->MakeStep(allWords,
                    allLastWords,
                    allStates,
                    /** out **/
                    allProbs,
                    allOutStates,
                    unks);
  
    //size_t k = 0;
    //for(Prefixes::iterator it = prefixes.begin(); it != prefixes.end(); it++) {
    //  BOOST_FOREACH(SP& hyp, it->second) {
    //    Payload& payload = hyp.second;
    //    payload.logProb_ = allProbs[k];
    //    payload.state_ = allOutStates[k];
    //    payload.known_ = unks[k];
    //    k++;
    //  }
    //}
  }
  std::cerr << "ok" << std::endl;
}

//void NeuralScoreFeature::BatchProcess(
//    const std::vector<std::string>& nextWords,
//    PyObject* pyContextVectors,
//    const std::vector<std::string>& lastWords,
//    std::vector<PyObject*>& inputStates,
//    std::vector<double>& logProbs,
//    std::vector<PyObject*>& nextStates,
//    std::vector<bool>& unks) {
//    
//    size_t items = nextWords.size();
//    size_t batches = ceil(items/(float)m_batchSize);
//    for(size_t i = 0; i < batches; ++i) {
//      size_t thisBatchStart = i * m_batchSize;
//      size_t thisBatchEnd = std::min(thisBatchStart + m_batchSize, items);
//    
//      //if(items > m_batchSize)
//      //  std::cerr << "b:" << i << ":" << thisBatchStart << "-" << thisBatchEnd << " ";
//    
//      std::vector<std::string> nextWordsBatch(nextWords.begin() + thisBatchStart,
//                                              nextWords.begin() + thisBatchEnd);
//      std::vector<std::string> lastWordsBatch(lastWords.begin() + thisBatchStart,
//                                              lastWords.begin() + thisBatchEnd);
//      std::vector<PyObject*> inputStatesBatch(inputStates.begin() + thisBatchStart,
//                                              inputStates.begin() + thisBatchEnd);
//      std::vector<bool> unksBatch;
//      std::vector<double> logProbsBatch;
//      std::vector<PyObject*> nextStatesBatch;
//      m_wrapper->GetNextLogProbStates(nextWordsBatch,
//                                    pyContextVectors,
//                                    lastWordsBatch,
//                                    inputStatesBatch,
//                                    logProbsBatch,
//                                    nextStatesBatch,
//                                    unksBatch);
//    
//      logProbs.insert(logProbs.end(), logProbsBatch.begin(), logProbsBatch.end());
//      nextStates.insert(nextStates.end(), nextStatesBatch.begin(), nextStatesBatch.end());
//      unks.insert(unks.end(), unksBatch.begin(), unksBatch.end());
//    }
//}


void NeuralScoreFeature::EvaluateInIsolation(const Phrase &source
    , const TargetPhrase &targetPhrase
    , ScoreComponentCollection &scoreBreakdown
    , ScoreComponentCollection &estimatedFutureScore) const
{}

void NeuralScoreFeature::EvaluateWithSourceContext(const InputType &input
    , const InputPath &inputPath
    , const TargetPhrase &targetPhrase
    , const StackVec *stackVec
    , ScoreComponentCollection &scoreBreakdown
    , ScoreComponentCollection *estimatedFutureScore) const
{}

void NeuralScoreFeature::EvaluateTranslationOptionListWithSourceContext(const InputType &input
    , const TranslationOptionList &translationOptionList) const
{}

FFState* NeuralScoreFeature::EvaluateWhenApplied(
  const Hypothesis& cur_hypo,
  const FFState* prev_state,
  ScoreComponentCollection* accumulator) const
{
  //NeuralScoreState* prevState = static_cast<NeuralScoreState*>(
  //                                const_cast<FFState*>(prev_state));
  
  std::vector<float> newScores(m_numScoreComponents, 0);
  
  //PyObject* nextState = NULL;
  //const TargetPhrase& tp = cur_hypo.GetCurrTargetPhrase();
  //Prefix phrase;
  //
  //for(size_t i = 0; i < tp.GetSize(); ++i) {
  //  std::string word = tp.GetWord(i).GetString(m_factor).as_string();
  //  phrase.push_back(word);
  //}
  //if(cur_hypo.IsSourceCompleted()) {
  //  phrase.push_back("</s>");
  //}
  //
  //int prevId = cur_hypo.GetPrevHypo()->GetId();
  //
  //if(!m_preCalc) {
  //  double prob = 0;
  //  m_wrapper->GetProb(phrase, context,
  //                     prevState->GetLastWord(),
  //                     prevState->GetState(),
  //                     prob, nextState);
  //  prevState = new NeuralScoreState(context, phrase, nextState);
  //  nextState = NULL;
  //
  //  newScores[0] = prob;
  //  newScores[1] = 0;
  //}
  //else {
  //  double prob = 0;
  //  size_t unks = 0;
  //  PyObject* state = 0;
  //  Prefix prefix;
  //  for(size_t i = 0; i < phrase.size(); i++) {
  //    prefix.push_back(phrase[i]);
  //    //std::cerr << "p3:" << const_cast<PrefsByLength&>(m_pbl)[prefix.size() - 1][prefix].count(prevId) << std::endl;
  //    if(!const_cast<PrefsByLength&>(m_pbl)[prefix.size() - 1][prefix].count(prevId)) {
  //      BOOST_FOREACH(std::string s, prefix) {
  //        std::cerr << s << " ";
  //      }
  //      std::cerr << std::endl;
  //    }
  //
  //    Payload& payload = const_cast<PrefsByLength&>(m_pbl)[prefix.size() - 1][prefix][prevId];
  //    state = payload.state_;
  //    unks += payload.known_ ? 0 : 1;
  //    prob += payload.logProb_;
  //  }
  //  prevState = new NeuralScoreState(context, phrase, state);
  //  nextState = NULL;
  //  newScores[0] = prob;
  //  newScores[1] = unks;
  //}
  
  accumulator->PlusEquals(this, newScores);
  
  //prevState->LimitLength(m_stateLength);
  NeuralScoreState* prevState = new NeuralScoreState(WhichState(0,0), "");
  return prevState;
}

FFState* NeuralScoreFeature::EvaluateWhenApplied(
  const ChartHypothesis& /* cur_hypo */,
  int /* featureID - used to index the state in the previous hypotheses */,
  ScoreComponentCollection* accumulator) const
{
  return new NeuralScoreState(WhichState(0,0), "");
}

void NeuralScoreFeature::SetParameter(const std::string& key, const std::string& value)
{
  if (key == "state-length") {
    m_stateLength = Scan<size_t>(value);
  } else if (key == "batch-size") {
    m_batchSize = Scan<size_t>(value);
  } else if (key == "model") {
    m_modelPath = value;
  } else if (key == "source-vocab") {
    m_sourceVocabPath = value;
  } else if (key == "target-vocab") {
    m_targetVocabPath = value;
  } else {
    StatefulFeatureFunction::SetParameter(key, value);
  }
}

}

