/********************************************************************
* Copyright (C) 2015 by Interstel Technologies, Inc.
*   and Hawaii Space Flight Laboratory.
*
* This file is part of the COSMOS/core that is the central
* module for COSMOS. For more information on COSMOS go to
* <http://cosmos-project.com>
*
* The COSMOS/core software is licenced under the
* GNU Lesser General Public License (LGPL) version 3 licence.
*
* You should have received a copy of the
* GNU Lesser General Public License
* If not, go to <http://www.gnu.org/licenses/>
*
* COSMOS/core is free software: you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation, either version 3 of
* the License, or (at your option) any later version.
*
* COSMOS/core is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* Refer to the "licences" folder for further information on the
* condititons and terms to use this software.
********************************************************************/

/*! \file equation.cpp
    \brief Infix equations over Namespace 2.0 names
*/

#include "support/equation.h"
#include "support/cosmos-errno.h"

namespace Cosmos
{
    namespace Support
    {
        int32_t Equation::parse(const string &text)
        {
            m_text = text;
            m_error.clear();
            terms.clear();
            root = -1;
            pos = 0;

            int32_t idx = parse_or();
            if (idx < 0)
            {
                terms.clear();
                return JSON_ERROR_SCAN;
            }
            skip();
            if (pos < m_text.size())
            {
                fail("unexpected '" + m_text.substr(pos, 1) + "'");
                terms.clear();
                return JSON_ERROR_SCAN;
            }
            root = idx;
            return 0;
        }

        double Equation::evaluate(const cosmosstruc *cinfo) const
        {
            if (root < 0 || cinfo == nullptr)
            {
                return NAN;
            }
            return eval(root, cinfo);
        }

        vector<string> Equation::names() const
        {
            vector<string> result;
            for (const Term &term : terms)
            {
                if (term.op == Op::Name)
                {
                    result.push_back(term.name);
                }
            }
            return result;
        }

        int32_t Equation::fail(const string &message)
        {
            if (m_error.empty())
            {
                m_error = message + " at " + std::to_string(pos);
            }
            return -1;
        }

        void Equation::skip()
        {
            while (pos < m_text.size() && isspace(static_cast<unsigned char>(m_text[pos])))
            {
                ++pos;
            }
        }

        bool Equation::accept(const char *token)
        {
            skip();
            size_t len = strlen(token);
            if (m_text.compare(pos, len, token) == 0)
            {
                pos += len;
                return true;
            }
            return false;
        }

        // A keyword only when it stands alone, so "andover" stays a name
        bool Equation::accept_word(const char *word)
        {
            skip();
            size_t len = strlen(word);
            if (m_text.compare(pos, len, word) != 0)
            {
                return false;
            }
            size_t end = pos + len;
            if (end < m_text.size() && (isalnum(static_cast<unsigned char>(m_text[end])) || m_text[end] == '_' || m_text[end] == '.' || m_text[end] == '['))
            {
                return false;
            }
            pos = end;
            return true;
        }

        int32_t Equation::add(Term term)
        {
            terms.push_back(term);
            return static_cast<int32_t>(terms.size() - 1);
        }

        int32_t Equation::parse_or()
        {
            int32_t left = parse_and();
            while (left >= 0 && (accept("||") || accept("|") || accept_word("or")))
            {
                int32_t right = parse_and();
                if (right < 0) return -1;
                Term term; term.op = Op::Or; term.left = left; term.right = right;
                left = add(term);
            }
            return left;
        }

        int32_t Equation::parse_and()
        {
            int32_t left = parse_not();
            while (left >= 0 && (accept("&&") || accept("&") || accept_word("and")))
            {
                int32_t right = parse_not();
                if (right < 0) return -1;
                Term term; term.op = Op::And; term.left = left; term.right = right;
                left = add(term);
            }
            return left;
        }

        int32_t Equation::parse_not()
        {
            skip();
            if ((m_text.compare(pos, 2, "!=") != 0 && accept("!")) || accept_word("not"))
            {
                int32_t operand = parse_not();
                if (operand < 0) return -1;
                Term term; term.op = Op::Not; term.left = operand;
                return add(term);
            }
            return parse_compare();
        }

        int32_t Equation::parse_compare()
        {
            int32_t left = parse_sum();
            if (left < 0) return -1;

            Op op;
            if (accept("<=")) op = Op::Le;
            else if (accept(">=")) op = Op::Ge;
            else if (accept("==")) op = Op::Eq;
            else if (accept("!=")) op = Op::Ne;
            else if (accept("<")) op = Op::Lt;
            else if (accept(">")) op = Op::Gt;
            else if (accept("=")) op = Op::Eq;
            else return left;

            int32_t right = parse_sum();
            if (right < 0) return -1;
            skip();
            if (pos < m_text.size() && strchr("<>=", m_text[pos]) != nullptr)
            {
                return fail("chained comparison; join with 'and'");
            }
            Term term; term.op = op; term.left = left; term.right = right;
            return add(term);
        }

        int32_t Equation::parse_sum()
        {
            int32_t left = parse_product();
            while (left >= 0)
            {
                Op op;
                if (accept("+")) op = Op::Add;
                else if (accept("-")) op = Op::Sub;
                else break;
                int32_t right = parse_product();
                if (right < 0) return -1;
                Term term; term.op = op; term.left = left; term.right = right;
                left = add(term);
            }
            return left;
        }

        int32_t Equation::parse_product()
        {
            int32_t left = parse_unary();
            while (left >= 0)
            {
                Op op;
                if (accept("*")) op = Op::Mul;
                else if (accept("/")) op = Op::Div;
                else if (accept("%")) op = Op::Mod;
                else break;
                int32_t right = parse_unary();
                if (right < 0) return -1;
                Term term; term.op = op; term.left = left; term.right = right;
                left = add(term);
            }
            return left;
        }

        int32_t Equation::parse_unary()
        {
            if (accept("-"))
            {
                int32_t operand = parse_unary();
                if (operand < 0) return -1;
                Term term; term.op = Op::Neg; term.left = operand;
                return add(term);
            }
            if (accept("+"))
            {
                return parse_unary();
            }
            return parse_power();
        }

        int32_t Equation::parse_power()
        {
            int32_t base = parse_primary();
            if (base >= 0 && accept("^"))
            {
                int32_t exponent = parse_unary();
                if (exponent < 0) return -1;
                Term term; term.op = Op::Pow; term.left = base; term.right = exponent;
                return add(term);
            }
            return base;
        }

        int32_t Equation::parse_primary()
        {
            skip();
            if (pos >= m_text.size())
            {
                return fail("unexpected end");
            }

            char c = m_text[pos];
            if (c == '(')
            {
                ++pos;
                int32_t inner = parse_or();
                if (inner < 0) return -1;
                if (!accept(")")) return fail("missing ')'");
                return inner;
            }

            if (isdigit(static_cast<unsigned char>(c)) || c == '.')
            {
                const char *start = m_text.c_str() + pos;
                char *end = nullptr;
                double value = strtod(start, &end);
                if (end == start) return fail("bad number");
                pos += static_cast<size_t>(end - start);
                Term term; term.op = Op::Constant; term.value = value;
                return add(term);
            }

            if (c == '"')
            {
                size_t close = m_text.find('"', pos + 1);
                if (close == string::npos) return fail("missing closing '\"'");
                Term term; term.op = Op::Name; term.name = m_text.substr(pos + 1, close - pos - 1);
                pos = close + 1;
                return add(term);
            }

            if (isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                string name;
                while (pos < m_text.size())
                {
                    c = m_text[pos];
                    if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')
                    {
                        name += c;
                        ++pos;
                    }
                    else if (c == '[')
                    {
                        size_t close = m_text.find(']', pos);
                        if (close == string::npos) return fail("missing ']'");
                        name += m_text.substr(pos, close - pos + 1);
                        pos = close + 1;
                    }
                    else
                    {
                        break;
                    }
                }
                Term term; term.op = Op::Name; term.name = name;
                return add(term);
            }

            return fail("unexpected '" + string(1, c) + "'");
        }

        double Equation::eval(int32_t idx, const cosmosstruc *cinfo) const
        {
            const Term &term = terms[idx];
            switch (term.op)
            {
            case Op::Constant:
                return term.value;
            case Op::Name:
                return cinfo->get_number(term.name);
            case Op::Neg:
                return -eval(term.left, cinfo);
            case Op::Not:
                {
                    double a = eval(term.left, cinfo);
                    return std::isnan(a) ? NAN : (a == 0. ? 1. : 0.);
                }
            default:
                break;
            }

            double a = eval(term.left, cinfo);
            double b = eval(term.right, cinfo);
            // NAN must not compare as false and then be negated or or-ed into a true
            if (std::isnan(a) || std::isnan(b))
            {
                return NAN;
            }
            switch (term.op)
            {
            case Op::Add: return a + b;
            case Op::Sub: return a - b;
            case Op::Mul: return a * b;
            case Op::Div: return a / b;
            case Op::Mod: return fmod(a, b);
            case Op::Pow: return pow(a, b);
            case Op::Lt:  return a < b ? 1. : 0.;
            case Op::Le:  return a <= b ? 1. : 0.;
            case Op::Gt:  return a > b ? 1. : 0.;
            case Op::Ge:  return a >= b ? 1. : 0.;
            case Op::Eq:  return a == b ? 1. : 0.;
            case Op::Ne:  return a != b ? 1. : 0.;
            case Op::And: return (a != 0. && b != 0.) ? 1. : 0.;
            case Op::Or:  return (a != 0. || b != 0.) ? 1. : 0.;
            default:      return NAN;
            }
        }
    }
}
