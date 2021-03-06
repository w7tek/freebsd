//===--------------------- JSON.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/JSON.h"

#include <limits.h>
#include "lldb/Core/StreamString.h"
#include "lldb/Host/StringConvert.h"

using namespace lldb_private;

std::string
JSONString::json_string_quote_metachars (const std::string &s)
{
    if (s.find('"') == std::string::npos)
        return s;
    
    std::string output;
    const size_t s_size = s.size();
    const char *s_chars = s.c_str();
    for (size_t i = 0; i < s_size; i++)
    {
        unsigned char ch = *(s_chars + i);
        if (ch == '"')
        {
            output.push_back ('\\');
        }
        output.push_back (ch);
    }
    return output;
}

JSONString::JSONString () :
    JSONValue(JSONValue::Kind::String),
    m_data()
{
}

JSONString::JSONString (const char* s) :
    JSONValue(JSONValue::Kind::String),
    m_data(s ? s : "")
{
}

JSONString::JSONString (const std::string& s) :
    JSONValue(JSONValue::Kind::String),
    m_data(s)
{
}

void
JSONString::Write (Stream& s)
{
    s.Printf("\"%s\"", json_string_quote_metachars(m_data).c_str());
}

JSONNumber::JSONNumber () :
    JSONValue(JSONValue::Kind::Number),
    m_is_integer(true),
    m_data(0),
    m_double(0.0)
{
}

JSONNumber::JSONNumber (uint64_t i) :
    JSONValue(JSONValue::Kind::Number),
    m_is_integer(true),
    m_data(i),
    m_double(0.0)
{
}


JSONNumber::JSONNumber (double d) :
    JSONValue(JSONValue::Kind::Number),
    m_is_integer(false),
    m_data(0),
    m_double(d)
{
}

void
JSONNumber::Write (Stream& s)
{
    if (m_is_integer)
        s.Printf("%" PRIu64, m_data);
    else
        s.Printf("%g", m_double);
}

JSONTrue::JSONTrue () :
    JSONValue(JSONValue::Kind::True)
{
}

void
JSONTrue::Write(Stream& s)
{
    s.Printf("true");
}

JSONFalse::JSONFalse () :
    JSONValue(JSONValue::Kind::False)
{
}

void
JSONFalse::Write(Stream& s)
{
    s.Printf("false");
}

JSONNull::JSONNull () :
    JSONValue(JSONValue::Kind::Null)
{
}

void
JSONNull::Write(Stream& s)
{
    s.Printf("null");
}

JSONObject::JSONObject () :
    JSONValue(JSONValue::Kind::Object)
{
}

void
JSONObject::Write (Stream& s)
{
    bool first = true;
    s.PutChar('{');
    auto iter = m_elements.begin(), end = m_elements.end();
    for (;iter != end; iter++)
    {
        if (first)
            first = false;
        else
            s.PutChar(',');
        JSONString key(iter->first);
        JSONValue::SP value(iter->second);
        key.Write(s);
        s.PutChar(':');
        value->Write(s);
    }
    s.PutChar('}');
}

bool
JSONObject::SetObject (const std::string& key,
                       JSONValue::SP value)
{
    if (key.empty() || nullptr == value.get())
        return false;
    m_elements[key] = value;
    return true;
}

JSONValue::SP
JSONObject::GetObject (const std::string& key)
{
    auto iter = m_elements.find(key), end = m_elements.end();
    if (iter == end)
        return JSONValue::SP();
    return iter->second;
}

JSONArray::JSONArray () :
    JSONValue(JSONValue::Kind::Array)
{
}

void
JSONArray::Write (Stream& s)
{
    bool first = true;
    s.PutChar('[');
    auto iter = m_elements.begin(), end = m_elements.end();
    for (;iter != end; iter++)
    {
        if (first)
            first = false;
        else
            s.PutChar(',');
        (*iter)->Write(s);
    }
    s.PutChar(']');
}

bool
JSONArray::SetObject (Index i,
                      JSONValue::SP value)
{
    if (value.get() == nullptr)
        return false;
    if (i < m_elements.size())
    {
        m_elements[i] = value;
        return true;
    }
    if (i == m_elements.size())
    {
        m_elements.push_back(value);
        return true;
    }
    return false;
}

bool
JSONArray::AppendObject (JSONValue::SP value)
{
    if (value.get() == nullptr)
        return false;
    m_elements.push_back(value);
    return true;
}

JSONValue::SP
JSONArray::GetObject (Index i)
{
    if (i < m_elements.size())
        return m_elements[i];
    return JSONValue::SP();
}

JSONArray::Size
JSONArray::GetNumElements ()
{
    return m_elements.size();
}


JSONParser::JSONParser (const char *cstr) :
    StringExtractor(cstr)
{
}

JSONParser::Token
JSONParser::GetToken (std::string &value)
{
    StreamString error;

    value.clear();
    SkipSpaces ();
    const uint64_t start_index = m_index;
    const char ch = GetChar();
    switch (ch)
    {
        case '{': return Token::ObjectStart;
        case '}': return Token::ObjectEnd;
        case '[': return Token::ArrayStart;
        case ']': return Token::ArrayEnd;
        case ',': return Token::Comma;
        case ':': return Token::Colon;
        case '\0': return Token::EndOfFile;
        case 't':
            if (GetChar() == 'r')
                if (GetChar() == 'u')
                    if (GetChar() == 'e')
                        return Token::True;
            break;

        case 'f':
            if (GetChar() == 'a')
                if (GetChar() == 'l')
                    if (GetChar() == 's')
                        if (GetChar() == 'e')
                            return Token::False;
            break;

        case 'n':
            if (GetChar() == 'u')
                if (GetChar() == 'l')
                    if (GetChar() == 'l')
                        return Token::Null;
            break;

        case '"':
            {
                while (1)
                {
                    bool was_escaped = false;
                    int escaped_ch = GetEscapedChar(was_escaped);
                    if (escaped_ch == -1)
                    {
                        error.Printf("error: an error occurred getting a character from offset %" PRIu64, start_index);
                        value = std::move(error.GetString());
                        return Token::Error;

                    }
                    else
                    {
                        const bool is_end_quote = escaped_ch == '"';
                        const bool is_null = escaped_ch == 0;
                        if (was_escaped || (!is_end_quote && !is_null))
                        {
                            if (CHAR_MIN <= escaped_ch && escaped_ch <= CHAR_MAX)
                            {
                                value.append(1, (char)escaped_ch);
                            }
                            else
                            {
                                error.Printf("error: wide character support is needed for unicode character 0x%4.4x at offset %" PRIu64, escaped_ch, start_index);
                                value = std::move(error.GetString());
                                return Token::Error;
                            }
                        }
                        else if (is_end_quote)
                        {
                            return Token::String;
                        }
                        else if (is_null)
                        {
                            value = "error: missing end quote for string";
                            return Token::Error;
                        }
                    }
                }
            }
            break;

        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            {
                bool done = false;
                bool got_decimal_point = false;
                uint64_t exp_index = 0;
                bool got_int_digits = (ch >= '0') && (ch <= '9');
                bool got_frac_digits = false;
                bool got_exp_digits = false;
                while (!done)
                {
                    const char next_ch = PeekChar();
                    switch (next_ch)
                    {
                        case '0':
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                        case '7':
                        case '8':
                        case '9':
                            if (exp_index != 0)
                            {
                                got_exp_digits = true;
                            }
                            else if (got_decimal_point)
                            {
                                got_frac_digits = true;
                            }
                            else
                            {
                                got_int_digits = true;
                            }
                            ++m_index; // Skip this character
                            break;

                        case '.':
                            if (got_decimal_point)
                            {
                                error.Printf("error: extra decimal point found at offset %" PRIu64, start_index);
                                value = std::move(error.GetString());
                                return Token::Error;
                            }
                            else
                            {
                                got_decimal_point = true;
                                ++m_index; // Skip this character
                            }
                            break;

                        case 'e':
                        case 'E':
                            if (exp_index != 0)
                            {
                                error.Printf("error: extra expenent character found at offset %" PRIu64, start_index);
                                value = std::move(error.GetString());
                                return Token::Error;
                            }
                            else
                            {
                                exp_index = m_index;
                                ++m_index; // Skip this character
                            }
                            break;

                        case '+':
                        case '-':
                            // The '+' and '-' can only come after an exponent character...
                            if (exp_index == m_index - 1)
                            {
                                ++m_index; // Skip the exponent sign character
                            }
                            else
                            {
                                error.Printf("error: unexpected %c character at offset %" PRIu64, next_ch, start_index);
                                value = std::move(error.GetString());
                                return Token::Error;
                            }

                        default:
                            done = true;
                            break;
                    }
                }

                if (m_index > start_index)
                {
                    value = m_packet.substr(start_index, m_index - start_index);
                    if (got_decimal_point)
                    {
                        if (exp_index != 0)
                        {
                            // We have an exponent, make sure we got exponent digits
                            if (got_exp_digits)
                            {
                                return Token::Float;
                            }
                            else
                            {
                                error.Printf("error: got exponent character but no exponent digits at offset in float value \"%s\"", value.c_str());
                                value = std::move(error.GetString());
                                return Token::Error;
                            }
                        }
                        else
                        {
                            // No exponent, but we need at least one decimal after the decimal point
                            if (got_frac_digits)
                            {
                                return Token::Float;
                            }
                            else
                            {
                                error.Printf("error: no digits after decimal point \"%s\"", value.c_str());
                                value = std::move(error.GetString());
                                return Token::Error;
                            }
                        }
                    }
                    else
                    {
                        // No decimal point
                        if (got_int_digits)
                        {
                            // We need at least some integer digits to make an integer
                            return Token::Integer;
                        }
                        else
                        {
                            error.Printf("error: no digits negate sign \"%s\"", value.c_str());
                            value = std::move(error.GetString());
                            return Token::Error;
                        }
                    }
                }
                else
                {
                    error.Printf("error: invalid number found at offset %" PRIu64, start_index);
                    value = std::move(error.GetString());
                    return Token::Error;
                }
            }
            break;
        default:
            break;
    }
    error.Printf("error: failed to parse token at offset %" PRIu64 " (around character '%c')", start_index, ch);
    value = std::move(error.GetString());
    return Token::Error;
}

int
JSONParser::GetEscapedChar(bool &was_escaped)
{
    was_escaped = false;
    const char ch = GetChar();
    if (ch == '\\')
    {
        was_escaped = true;
        const char ch2 = GetChar();
        switch (ch2)
        {
            case '"':
            case '\\':
            case '/':
            default:
                break;

            case 'b': return '\b';
            case 'f': return '\f';
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case 'u':
                {
                    const int hi_byte = DecodeHexU8();
                    const int lo_byte = DecodeHexU8();
                    if (hi_byte >=0 && lo_byte >= 0)
                        return hi_byte << 8 | lo_byte;
                    return -1;
                }
                break;
        }
        return ch2;
    }
    return ch;
}

JSONValue::SP
JSONParser::ParseJSONObject ()
{
    // The "JSONParser::Token::ObjectStart" token should have already been consumed
    // by the time this function is called
    std::unique_ptr<JSONObject> dict_up(new JSONObject());

    std::string value;
    std::string key;
    while (1)
    {
        JSONParser::Token token = GetToken(value);

        if (token == JSONParser::Token::String)
        {
            key.swap(value);
            token = GetToken(value);
            if (token == JSONParser::Token::Colon)
            {
                JSONValue::SP value_sp = ParseJSONValue();
                if (value_sp)
                    dict_up->SetObject(key, value_sp);
                else
                    break;
            }
        }
        else if (token == JSONParser::Token::ObjectEnd)
        {
            return JSONValue::SP(dict_up.release());
        }
        else if (token == JSONParser::Token::Comma)
        {
            continue;
        }
        else
        {
            break;
        }
    }
    return JSONValue::SP();
}

JSONValue::SP
JSONParser::ParseJSONArray ()
{
    // The "JSONParser::Token::ObjectStart" token should have already been consumed
    // by the time this function is called
    std::unique_ptr<JSONArray> array_up(new JSONArray());

    std::string value;
    std::string key;
    while (1)
    {
        JSONValue::SP value_sp = ParseJSONValue();
        if (value_sp)
            array_up->AppendObject(value_sp);
        else
            break;

        JSONParser::Token token = GetToken(value);
        if (token == JSONParser::Token::Comma)
        {
            continue;
        }
        else if (token == JSONParser::Token::ArrayEnd)
        {
            return JSONValue::SP(array_up.release());
        }
        else
        {
            break;
        }
    }
    return JSONValue::SP();
}

JSONValue::SP
JSONParser::ParseJSONValue ()
{
    std::string value;
    const JSONParser::Token token = GetToken(value);
    switch (token)
    {
        case JSONParser::Token::ObjectStart:
            return ParseJSONObject();

        case JSONParser::Token::ArrayStart:
            return ParseJSONArray();

        case JSONParser::Token::Integer:
        {
            bool success = false;
            uint64_t uval = StringConvert::ToUInt64(value.c_str(), 0, 0, &success);
            if (success)
                return JSONValue::SP(new JSONNumber(uval));
        }
            break;

        case JSONParser::Token::Float:
        {
            bool success = false;
            double val = StringConvert::ToDouble(value.c_str(), 0.0, &success);
            if (success)
                return JSONValue::SP(new JSONNumber(val));
        }
            break;

        case JSONParser::Token::String:
            return JSONValue::SP(new JSONString(value));

        case JSONParser::Token::True:
            return JSONValue::SP(new JSONTrue());

        case JSONParser::Token::False:
            return JSONValue::SP(new JSONFalse());

        case JSONParser::Token::Null:
            return JSONValue::SP(new JSONNull());

        default:
            break;
    }
    return JSONValue::SP();
    
}
