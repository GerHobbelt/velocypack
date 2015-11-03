////////////////////////////////////////////////////////////////////////////////
/// @brief Library to build up Jason documents.
///
/// DISCLAIMER
///
/// Copyright 2015 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Max Neunhoeffer
/// @author Jan Steemann
/// @author Copyright 2015, ArangoDB GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "JasonParser.h"

using namespace arangodb::jason;

// The following function does the actual parse. It gets bytes
// via peek, consume and reset appends the result to the JasonBuilder
// in _b. Errors are reported via an exception.
// Behind the scenes it runs two parses, one to collect sizes and
// check for parse errors (scan phase) and then one to actually
// build the result (build phase).

JasonLength JasonParser::parseInternal (bool multi) {
  _b.options = options; // copy over options

  // skip over optional BOM
  if (_size >= 3 &&
      _start[0] == 0xef &&
      _start[1] == 0xbb &&
      _start[2] == 0xbf) {
    // found UTF-8 BOM. simply skip over it
    _pos += 3;
  }

  JasonLength nr = 0;
  do {
    parseJson();
    nr++;
    while (_pos < _size && 
           isWhiteSpace(_start[_pos])) {
      ++_pos;
    }
    if (! multi && _pos != _size) {
      consume();  // to get error reporting right
      throw JasonException(JasonException::ParseError, "Expecting EOF");
    }
  } 
  while (multi && _pos < _size);
  return nr;
}

void JasonParser::parseNumber () {
  ParsedNumber numberValue;
  bool negative = false;
  int i = consume();
  // We know that a character is coming, and it's a number if it
  // starts with '-' or a digit. otherwise it's invalid
  if (i == '-') {
    i = getOneOrThrow("Incomplete number");
    negative = true;
  }
  if (i < '0' || i > '9') {
    throw JasonException(JasonException::ParseError, "Expecting digit");
  }
  
  if (i != '0') {
    unconsume();
    scanDigits(numberValue);
  }
  i = consume();
  if (i < 0 || i != '.') {
    if (i >= 0) {
      unconsume();
    }
    if (! numberValue.isInteger) {
      if (negative) {
        _b.addDouble(-numberValue.doubleValue);
      }
      else {
        _b.addDouble(numberValue.doubleValue);
      }
    }
    else if (negative) {
      if (numberValue.intValue <= static_cast<uint64_t>(INT64_MAX)) {
        _b.addInt(-static_cast<int64_t>(numberValue.intValue));
      }
      else if (numberValue.intValue == toUInt64(INT64_MIN)) {
        _b.addInt(INT64_MIN);
      }
      else {
        _b.addDouble(-static_cast<double>(numberValue.intValue));
      }
    }
    else {
      _b.addUInt(numberValue.intValue);
    }
    return;
  }
  i = getOneOrThrow("Incomplete number");
  if (i < '0' || i > '9') {
    throw JasonException(JasonException::ParseError, "Incomplete number");
  }
  unconsume();
  double fractionalPart = scanDigitsFractional();
  if (negative) {
    fractionalPart = - numberValue.asDouble() - fractionalPart;
  }
  else {
    fractionalPart = numberValue.asDouble() + fractionalPart;
  }
  i = consume();
  if (i < 0) {
    _b.addDouble(fractionalPart);
    return;
  }
  if (i != 'e' && i != 'E') {
    unconsume();
    _b.addDouble(fractionalPart);
    return;
  }
  i = getOneOrThrow("Incomplete number");
  negative = false;
  if (i == '+' || i == '-') {
    negative = (i == '-');
    i = getOneOrThrow("Incomplete number");
  }
  if (i < '0' || i > '9') {
    throw JasonException(JasonException::ParseError, "Incomplete number");
  }
  unconsume();
  ParsedNumber exponent;
  scanDigits(exponent);
  if (negative) {
    fractionalPart *= pow(10, -exponent.asDouble());
  }
  else {
    fractionalPart *= pow(10, exponent.asDouble());
  }
  if (std::isnan(fractionalPart) || ! std::isfinite(fractionalPart)) {
    throw JasonException(JasonException::NumberOutOfRange);
  }
  _b.addDouble(fractionalPart);
}

void JasonParser::parseString () {
  // When we get here, we have seen a " character and now want to
  // find the end of the string and parse the string value to its
  // Jason representation. We assume that the string is short and
  // insert 8 bytes for the length as soon as we reach 127 bytes
  // in the Jason representation.

  JasonLength const base = _b._pos;
  _b.reserveSpace(1);
  _b._start[_b._pos++] = 0x40;   // correct this later

  bool large = false;          // set to true when we reach 128 bytes
  uint32_t highSurrogate = 0;  // non-zero if high-surrogate was seen

  while (true) {
    size_t remainder = _size - _pos;
    if (remainder >= 16) {
      _b.reserveSpace(remainder);
      size_t count;
      if (options.validateUtf8Strings) {
        count = JSONStringCopyCheckUtf8(_b._start + _b._pos, _start + _pos,
                                        remainder);
      }
      else {
        count = JSONStringCopy(_b._start + _b._pos, _start + _pos, remainder);
      }
      _pos += count;
      _b._pos += count;
    }
    int i = getOneOrThrow("Unfinished string");
    if (! large && _b._pos - (base + 1) > 126) {
      large = true;
      _b.reserveSpace(8);
      memmove(_b._start + base + 9, _b._start + base + 1,
              _b._pos - (base + 1));
      _b._pos += 8;
    }
    switch (i) {
      case '"':
        JasonLength len;
        if (! large) {
          len = _b._pos - (base + 1);
          _b._start[base] = 0x40 + static_cast<uint8_t>(len);
          // String is ready
        }
        else {
          len = _b._pos - (base + 9);
          _b._start[base] = 0xbf;
          for (JasonLength i = 1; i <= 8; i++) {
            _b._start[base + i] = len & 0xff;
            len >>= 8;
          }
        }
        return;
      case '\\':
        // Handle cases or throw error
        i = consume();
        if (i < 0) {
          throw JasonException(JasonException::ParseError, "Invalid escape sequence");
        }
        switch (i) {
          case '"':
          case '/':
          case '\\':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = static_cast<uint8_t>(i);
            highSurrogate = 0;
            break;
          case 'b':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = '\b';
            highSurrogate = 0;
            break;
          case 'f':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = '\f';
            highSurrogate = 0;
            break;
          case 'n':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = '\n';
            highSurrogate = 0;
            break;
          case 'r':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = '\r';
            highSurrogate = 0;
            break;
          case 't':
            _b.reserveSpace(1);
            _b._start[_b._pos++] = '\t';
            highSurrogate = 0;
            break;
          case 'u': {
            uint32_t v = 0;
            for (int j = 0; j < 4; j++) {
              i = consume();
              if (i < 0) {
                throw JasonException(JasonException::ParseError, "Unfinished \\uXXXX escape sequence");
              }
              if (i >= '0' && i <= '9') {
                v = (v << 4) + i - '0';
              }
              else if (i >= 'a' && i <= 'f') {
                v = (v << 4) + i - 'a' + 10;
              }
              else if (i >= 'A' && i <= 'F') {
                v = (v << 4) + i - 'A' + 10;
              }
              else {
                throw JasonException(JasonException::ParseError, "Illegal \\uXXXX escape sequence");
              }
            }
            if (v < 0x80) {
              _b.reserveSpace(1);
              _b._start[_b._pos++] = static_cast<uint8_t>(v);
              highSurrogate = 0;
            }
            else if (v < 0x800) {
              _b.reserveSpace(2);
              _b._start[_b._pos++] = 0xc0 + (v >> 6);
              _b._start[_b._pos++] = 0x80 + (v & 0x3f);
              highSurrogate = 0;
            }
            else if (v >= 0xdc00 && v < 0xe000 &&
                     highSurrogate != 0) {
              // Low surrogate, put the two together:
              v = 0x10000 + ((highSurrogate - 0xd800) << 10)
                          + v - 0xdc00;
              _b._pos -= 3;
              _b.reserveSpace(4);
              _b._start[_b._pos++] = 0xf0 + (v >> 18);
              _b._start[_b._pos++] = 0x80 + ((v >> 12) & 0x3f);
              _b._start[_b._pos++] = 0x80 + ((v >> 6) & 0x3f);
              _b._start[_b._pos++] = 0x80 + (v & 0x3f);
              highSurrogate = 0;
            }
            else {
              if (v >= 0xd800 && v < 0xdc00) {
                // High surrogate:
                highSurrogate = v;
              }
              else {
                highSurrogate = 0;
              }
              _b.reserveSpace(3);
              _b._start[_b._pos++] = 0xe0 + (v >> 12);
              _b._start[_b._pos++] = 0x80 + ((v >> 6) & 0x3f);
              _b._start[_b._pos++] = 0x80 + (v & 0x3f);
            }
            break;
          }
          default:
            throw JasonException(JasonException::ParseError, "Invalid escape sequence");
        }
        break;
      default:
        if ((i & 0x80) == 0) {
          // non-UTF-8 sequence
          if (i < 0x20) {
            // control character
            throw JasonException(JasonException::UnexpectedControlCharacter);
          }
          highSurrogate = 0;
          _b.reserveSpace(1);
          _b._start[_b._pos++] = static_cast<uint8_t>(i);
        }
        else {
          if (! options.validateUtf8Strings) {
            highSurrogate = 0;
            _b.reserveSpace(1);
            _b._start[_b._pos++] = static_cast<uint8_t>(i);
          }
          else {
            // multi-byte UTF-8 sequence!
            int follow = 0;
            if ((i & 0xe0) == 0x80) {
              throw JasonException(JasonException::InvalidUtf8Sequence);
            }
            else if ((i & 0xe0) == 0xc0) {
              // two-byte sequence
              follow = 1;
            }
            else if ((i & 0xf0) == 0xe0) {
              // three-byte sequence
              follow = 2;
            }
            else if ((i & 0xf8) == 0xf0) {
              // four-byte sequence
              follow = 3;
            }
            else {
              throw JasonException(JasonException::InvalidUtf8Sequence);
            }

            // validate follow up characters
            _b.reserveSpace(1 + follow);
            _b._start[_b._pos++] = static_cast<uint8_t>(i);
            for (int j = 0; j < follow; ++j) {
              i = getOneOrThrow("scanString: truncated UTF-8 sequence");
              if ((i & 0xc0) != 0x80) {
                throw JasonException(JasonException::InvalidUtf8Sequence);
              }
              _b._start[_b._pos++] = static_cast<uint8_t>(i);
            }
            highSurrogate = 0;
          }
        }
        break;
    }
  }
}

void JasonParser::parseArray () {
  JasonLength base = _b._pos;
  _b.addArray();

  int i = skipWhiteSpace("Expecting item or ']'");
  if (i == ']') {
    // empty array
    ++_pos;   // the closing ']'
    _b.close();
    return;
  }

  while (true) {
    // parse array element itself
    _b.reportAdd(base);
    parseJson();
    i = skipWhiteSpace("Expecting ',' or ']'");
    if (i == ']') {
      // end of array
      ++_pos;  // the closing ']'
      _b.close();
      return;
    }
    // skip over ','
    if (i != ',') {
      throw JasonException(JasonException::ParseError, "Expecting ',' or ']'");
    }
    ++_pos;  // the ','
  }
}
                       
void JasonParser::parseObject () {
  JasonLength base = _b._pos;
  _b.addObject();

  int i = skipWhiteSpace("Expecting item or '}'");
  if (i == '}') {
    // empty object
    consume();   // the closing ']'
    _b.close();
    return;
  }

  while (true) {
    // always expecting a string attribute name here
    if (i != '"') {
      throw JasonException(JasonException::ParseError, "Expecting '\"' or '}'");
    }
    // get past the initial '"'
    ++_pos;

    _b.reportAdd(base);
    parseString();
    i = skipWhiteSpace("Expecting ':'");
    // always expecting the ':' here
    if (i != ':') {
      throw JasonException(JasonException::ParseError, "Expecting ':'");
    }
    ++_pos; // skip over the colon

    parseJson();
    i = skipWhiteSpace("Expecting ',' or '}'");
    if (i == '}') {
      // end of object
      ++_pos;  // the closing '}'
      _b.close();
      return;
    }
    if (i != ',') {
      throw JasonException(JasonException::ParseError, "Expecting ',' or '}'");
    } 
    // skip over ','
    ++_pos;  // the ','
    i = skipWhiteSpace("Expecting '\"' or '}'");
  }
}
                       
void JasonParser::parseJson () {
  skipWhiteSpace("Expecting item");

  int i = consume();
  if (i < 0) {
    return; 
  }
  switch (i) {
    case '{': 
      parseObject();  // this consumes the closing '}' or throws
      break;
    case '[':
      parseArray();   // this consumes the closing ']' or throws
      break;
    case 't':
      parseTrue();    // this consumes "rue" or throws
      break;
    case 'f':
      parseFalse();   // this consumes "alse" or throws
      break;
    case 'n':
      parseNull();    // this consumes "ull" or throws
      break;
    case '"': 
      parseString();
      break;
    default: {
      // everything else must be a number or is invalid...
      // this includes '-' and '0' to '9'. scanNumber() will
      // throw if the input is non-numeric
      unconsume();
      parseNumber();  // this consumes the number or throws
      break;
    }
  }
}
