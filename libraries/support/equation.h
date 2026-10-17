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

/*!
 * \file equation.h
 * \brief Infix equations over Namespace 2.0 names.
 * \ingroup cosmos_namespace
 *
 * Replaces the Namespace 1.0 equations (json_equation(), "node_loc_..." names,
 * two operands per bracket) for conditions. An equation is parsed once and
 * evaluated against any ::cosmosstruc, looking names up in Namespace 2.0 at
 * evaluation time, so it never holds a pointer that can go stale.
 *
 * Syntax, lowest precedence first:
 *  - `|`  `||`  `or`
 *  - `&`  `&&`  `and`
 *  - `!`  `not`                      (so `not a < b` is `not (a < b)`)
 *  - `<`  `<=`  `>`  `>=`  `=`  `==`  `!=`   (no chaining)
 *  - `+`  `-`
 *  - `*`  `/`  `%`
 *  - unary `-` `+`
 *  - `^`                             (right associative, binds tighter than unary minus)
 *  - number, name, `( ... )`
 *
 * A name is a Namespace 2.0 name: letters, digits, `_` and `.`, with bracketed
 * parts copied verbatim, so `node.loc.pos.geod.s.lat`, `target[3].elto` and
 * `target[Shackleton crater].elto` are all single names. A name in double
 * quotes is taken as is.
 *
 * Values are SI and radians, as stored. Comparisons and logic give 1 or 0. A
 * name that is missing or not numeric makes the whole result NAN, which
 * callers treat as false.
 */

#ifndef COSMOS_EQUATION_H
#define COSMOS_EQUATION_H

#include "support/configCosmos.h"
#include "support/jsondef.h"

namespace Cosmos
{
    namespace Support
    {
        class Equation
        {
        public:
            Equation() {}
            explicit Equation(const string &text) { parse(text); }

            //! Parse an infix equation.
            /*! \param text Equation text
                \return 0, or JSON_ERROR_SCAN with error() describing the problem
            */
            int32_t parse(const string &text);

            //! Evaluate against a node's state.
            /*! \param cinfo State whose Namespace 2.0 names are read
                \return Result, or NAN if not parsed, or any name is missing or not numeric
            */
            double evaluate(const cosmosstruc *cinfo) const;

            //! True if the last parse() succeeded.
            bool valid() const { return root >= 0; }
            //! Text given to the last parse().
            const string &text() const { return m_text; }
            //! Why the last parse() failed, or empty.
            const string &error() const { return m_error; }
            //! Every name the equation reads, in order of appearance.
            vector<string> names() const;

        private:
            enum class Op : uint8_t {
                Constant, Name,
                Neg, Not,
                Add, Sub, Mul, Div, Mod, Pow,
                Lt, Le, Gt, Ge, Eq, Ne,
                And, Or
            };
            struct Term
            {
                Op op = Op::Constant;
                double value = 0.;
                string name;
                int32_t left = -1;
                int32_t right = -1;
            };
            vector<Term> terms;
            int32_t root = -1;
            string m_text;
            string m_error;

            // Parser state, used only during parse()
            size_t pos = 0;
            void skip();
            bool accept(const char *token);
            bool accept_word(const char *word);
            int32_t add(Term term);
            int32_t parse_or();
            int32_t parse_and();
            int32_t parse_not();
            int32_t parse_compare();
            int32_t parse_sum();
            int32_t parse_product();
            int32_t parse_unary();
            int32_t parse_power();
            int32_t parse_primary();
            int32_t fail(const string &message);

            double eval(int32_t idx, const cosmosstruc *cinfo) const;
        };
    }
}

#endif // COSMOS_EQUATION_H
