/*!
 * \file enumlib.h
 * \brief Device type enum string lookup tables.
 * \ingroup cosmos_agent_namespace
 * \defgroup cosmos_enumlib Enum
 *
 * Maps COSMOS device-type integer enumerations (defined in jsondef.h) to
 * human-readable name strings and back. Use when serialising device types to
 * JSON or presenting them in user-facing logs and tools.
 */

#ifndef ENUMLIB_H
#define ENUMLIB_H
/*! \file enumlib.h
    \brief enumlib include file
    A library providing a better approach to providing enums.
*/


namespace Cosmos {
    namespace Support {
        class Enum
        {
        public:
            Enum();
            void Init(const vector<string> names, const vector<ptrdiff_t> values={});
            void Extend(const vector<string> names, const vector<ptrdiff_t> values={});
            bool Exists(string key);
            bool Exists(ptrdiff_t value);
            typedef map<string, ptrdiff_t> ForwardEnum;
            typedef map<ptrdiff_t, string> BackwardEnum;
            ptrdiff_t operator [](const string name);
            string operator [](const ptrdiff_t value);
            size_t Size();

        private:
            ForwardEnum forward;
            BackwardEnum backward;
        };
    }
}

#endif // ENUMLIB_H
