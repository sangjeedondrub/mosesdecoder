#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <limits>
#include <sstream>
#include <vector>

#define NOT_FOUND 			std::numeric_limits<size_t>::max()
typedef size_t FactorType;

template<typename T>
class UnorderedComparer
{
public:
  size_t operator()(const T& obj) const {
    return obj.hash();
  }

  bool operator()(const T& a, const T& b) const {
    return a == b;
  }

  size_t operator()(const T* obj) const {
    return obj->hash();
  }

  bool operator()(const T* a, const T* b) const {
    return (*a) == (*b);
  }

};

template<typename T>
void Init(T arr[], size_t size, const T &val) {
	for (size_t i = 0; i < size; ++i) {
		arr[i] = val;
	}
}


//! convert string to variable of type T. Used to reading floats, int etc from files
template<typename T>
inline T Scan(const std::string &input)
{
  std::stringstream stream(input);
  T ret;
  stream >> ret;
  return ret;
}

//! just return input
template<>
inline std::string Scan<std::string>(const std::string &input)
{
  return input;
}

//! Specialisation to understand yes/no y/n true/false 0/1
template<>
bool Scan<bool>(const std::string &input);

//! convert vectors of string to vectors of type T variables
template<typename T>
inline std::vector<T> Scan(const std::vector< std::string > &input)
{
  std::vector<T> output(input.size());
  for (size_t i = 0 ; i < input.size() ; i++) {
    output[i] = Scan<T>( input[i] );
  }
  return output;
}

//! speeded up version of above
template<typename T>
inline void Scan(std::vector<T> &output, const std::vector< std::string > &input)
{
  output.resize(input.size());
  for (size_t i = 0 ; i < input.size() ; i++) {
    output[i] = Scan<T>( input[i] );
  }
}

/** tokenise input string to vector of string. each element has been separated by a character in the delimiters argument.
		The separator can only be 1 character long. The default delimiters are space or tab
*/
inline std::vector<std::string> Tokenize(const std::string& str,
    const std::string& delimiters = " \t")
{
  std::vector<std::string> tokens;
  // Skip delimiters at beginning.
  std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  std::string::size_type pos     = str.find_first_of(delimiters, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    // Found a token, add it to the vector.
    tokens.push_back(str.substr(lastPos, pos - lastPos));
    // Skip delimiters.  Note the "not_of"
    lastPos = str.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = str.find_first_of(delimiters, lastPos);
  }

  return tokens;
}

//! tokenise input string to vector of type T
template<typename T>
inline std::vector<T> Tokenize( const std::string &input
                                , const std::string& delimiters = " \t")
{
  std::vector<std::string> stringVector = Tokenize(input, delimiters);
  return Scan<T>( stringVector );
}

//! get string representation of any object/variable, as long as it can pipe to a stream
template<typename T>
inline std::string SPrint(const T &input)
{
  std::stringstream stream("");
  stream << input;
  return stream.str();
}

//! delete white spaces at beginning and end of string
inline std::string Trim(const std::string& str, const std::string dropChars = " \t\n\r")
{
  std::string res = str;
  res.erase(str.find_last_not_of(dropChars)+1);
  return res.erase(0, res.find_first_not_of(dropChars));
}

inline float UntransformLMScore(float logNScore)
{
  // opposite of above
  return logNScore / 2.30258509299405f;
}

inline bool FileExists(const std::string& filePath)
{
	  std::ifstream ifs(filePath.c_str());
	  return !ifs.fail();
}

const std::string ToLower(const std::string& str);
