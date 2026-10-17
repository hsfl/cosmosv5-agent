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
 * \file event.h
 * \brief Scheduled events with JSON-serializable conditions and actions.
 * \ingroup cosmos_agent_framework
 * \defgroup cosmos_event Event
 *
 * Defines the Event class used by the COSMOS command and scheduling system. An
 * event has a JSON-encoded condition (time, state predicate) and a command
 * action string. Events can be loaded from files, queued, evaluated, and
 * executed by the scheduler.
 */

#ifndef COSMOS_COMMAND_H
#define COSMOS_COMMAND_H

/*! \file event.h
*	\brief Commanding Class
*/

//! \ingroup support
//! \defgroup agentlib Agent Server and Client Library
//! %Agent Server and Client.
//!


#include "support/configCosmos.h"
#include "support/jsonlib.h"
#include "support/equation.h"
//#include "support/jsonobject.h"
#include "support/timelib.h"

#define MAXCOMMANDWORD 20

namespace Cosmos
{
//    namespace Support
//    {

        /// Class to manage %Event information
        /**
    An %Event is a collection of information related to a single occurance.
*/
        class Event
        {

        public: // TODO: consider private?

            /**	%Event start time (Modified Julian Date) */
            double		mjd;
            /** %Event execution time (Coordinated Universal Time) -- JIMNOTE: but appears to be using MJD in the code? should it be named mjdexec? */
            double		utcexec=0.;
            /** %Event timeout (seconds) */
            double timeout=30.;
            /**	%Event name */
            string		name;
            /** %Event type */
            uint32_t	type;
            /** %Event flag */
            uint32_t	flag;
            /** %Event data */
            string		data;
            /** %Event condition: an infix Namespace 2.0 equation (see support/equation.h) */
            string		condition;
            /** \p condition, parsed on first use and whenever it changes */
            Support::Equation	equation;
            /**	%Event run indicators */
            bool		already_ran;
            uint32_t true_count=0;
            /** %Event information stored as a JSON string */
            string		event_string;

        public:

            /// Default constructor
            Event();

            ///	Destructor
            ~Event();

            ///	Sets %Event information from a JSON formatted string
            /**
        \param	jstring	Event information string formatted as JSON

        This function copies all Event information (from a JSON formatted string) into the current Event object
    */
            void	set_command(string jstring);

            ///	Sets Event::utcexec to the execution time
            /**
        This function is called after event execution and updates the execution time.
        \param	utc	Execution time (MJD); 0 means the current wall-clock time.
    */
            void	set_utcexec(double utc=0.)	{	utcexec = (utc == 0.) ? currentmjd(0.) : utc;	}

            ///	Sets Event::flag to indicate the event has actually executed (i.e. EVENT_FLAG_ACTUAL)
            /**
        This function is called after event execution and updates the execution status.
    */
            void	set_actual()	{	flag |= EVENT_FLAG_ACTUAL;	}

            ///	Retrieves Event::name
            /**
        \return string representing %Event name
    */
            string  get_name()		{	return name; }

            /// Retrieves Event information
            /**
        \return	string representing Event information (as a JSON formatted string)
    */
            string	get_event_string();

            ///	Retrieves Event::mjd
            /**
        \return double representing %Event start time
    */
            double	getUtc()		{	return mjd;	}

            ///	Retrieves Event::mjd
            /**
        \return double representing %Event start time
    */
            string	getTime()		{	return mjd2iso8601( getUtc() ); }

            ///	Retrieves Event::utcexec
            /**
        \return double representing %Event execution time
    */
            double	getUtcExec()	{	return utcexec;	}

            ///	Retrieves Event::data
            /**
        \return string representing %Event data
    */
            string	get_data()		{	return data; }

            ///	Determines if it is time for the %Event to execute
            /**
        \param	utc	Time to test against (MJD); 0 means the current wall-clock time.
        A simulator passes its simulated time here.
        \return	True if %Event is ready to execute, otherwise false
    */
            bool	is_ready(double utc=0.)	{	return (mjd <= ((utc == 0.) ? currentmjd(0.) : utc)); }

            ///	Determines if the %Event repeatable
            /**
        \return	True if %Event is repeatable, otherwise false
    */
            bool	is_repeat()		{	return (flag & EVENT_FLAG_REPEAT);	}

            ///	Determines if the %Event is a command
            /**
        \return	True if %Event is a command, otherwise false
    */
            bool	is_command()	{	return (type & EVENT_TYPE_COMMAND || type & EVENT_TYPE_REQUEST);	}
//            bool	is_request()	{	return (type & EVENT_TYPE_REQUEST);	}

            ///	Determines if the %Event is a conditional command
            /**
        \return	True if %Event is a conditional command, otherwise false
    */
            bool	is_conditional(){	return (flag & EVENT_FLAG_CONDITIONAL);	}

            ///	Determines if the %Event is a solo command
            /**
        \return	True if %Event is a solo command, otherwise false
    */
            bool	is_solo(){	return (flag & EVENT_FLAG_SOLO);	}

            ///	Determines if the %Event has already run
            /**
        \return	True if %Event has already run, otherwise false
    */
            bool	is_alreadyrun(){	return (flag & EVENT_FLAG_TRUE);	}
            void    set_alreadyrun(bool value){ if (value) flag |= EVENT_FLAG_TRUE; else flag &= ~EVENT_FLAG_TRUE; }

            string generator(
                    string name,
                    string data,
                    double utc,
                    string condition,
                    uint32_t flag,
                    uint16_t type
                    );

            string generator(eventstruc event);

            /// Evaluates the condition against a node's state, with debounce
            /**
        \param	cinfo	State whose Namespace 2.0 names the condition reads
        \return	True once the condition has been nonzero for the debounce count
        (from EVENT_FLAG_PRIORITY) of consecutive calls. A condition that does not
        parse, or reads a missing name, is false.
    */
            bool condition_true(cosmosstruc *cinfo);

            ///	Extraction operator
            /**
        \param	out	Reference to ostream
        \param	cmd	Reference to const Event
        \return	Reference to modified ostream

        Writes the given Event to the given output stream (in JSON format) and returns a reference to the modified ostream.
    */
            friend ::std::ostream& operator<<(::std::ostream& out, const Event& cmd);

            ///	Equality operator
            /**
        \param	cmd1	First event
        \param	cmd2	Second event
        \return	True if events are exactly the same, otherwise false
    */
            friend bool operator==(const Event& cmd1, const Event& cmd2);

        }; // end of class Event
//    } // end of namespace Support
} // end of namespace Cosmos

#endif // COSMOS_COMMAND_H
