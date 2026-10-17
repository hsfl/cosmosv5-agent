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

/*! \file agentclass.cpp
    \brief Agent support functions
*/

#include "agent/command_queue.h"

namespace Cosmos
{
    namespace Support
    {
        // *************************************************************************
        // Class: CommandQueue
        // *************************************************************************


        CommandQueue::~CommandQueue () {}

        //! Run the given Request Event
        /*!
            Submits a Request to the local agent. For each
            command run, the time of execution (utcexec) is set, the flag
            EVENT_FLAG_ACTUAL is set to true, and this updated command information is
            logged to the OUTPUT directory.

            \param	cmd	Reference to event to run
            \param	nodename	Name of node
            \param	logdate_exec	Time of execution (for logging purposes)
        */
        int32_t CommandQueue::run_request(Agent *agent, Event& cmd, string node_name, double logdate_exec)
        {
            int32_t iretn = 0;
            queue_changed = true;

            // utcexec and EVENT_FLAG_ACTUAL were set by due_commands()

            string outpath = data_type_path(node_name, "temp", "exec", logdate_exec, "out");
            string bufferout;
            string request = cmd.get_data();
            iretn = agent->process_request(request, bufferout, false);
            printf("CMD: running request [%s] in [%s] with result %d\n", request.c_str(), outpath.c_str(), iretn);
            if (iretn >= 0)
            {
                FILE *fp = fopen(outpath.c_str(), "w");
                if (fp != nullptr)
                {
                    fwrite(bufferout.data(), 1, bufferout.size(), fp);
                    fclose(fp);
                    log_move_file(outpath, string_replace(outpath, "temp", "outgoing"), true);
                    printf("CMD: moving request result to [%s]\n", string_replace(outpath, "temp", "outgoing").c_str());
                }
                else
                {
                    printf("CMD: failed to move request result to [%s] for %d\n", outpath.c_str(), errno);
                }
                // log to event file
                log_write(node_name, "exec", logdate_exec, "", "event", cmd.get_event_string().c_str(), "outgoing");
            }
            return iretn;
        }

        //! Run the given Command Event
        /*!
                Executes a command in a separate shell (system) using threads. For each
                command run, the time of execution (utcexec) is set, the flag
                EVENT_FLAG_ACTUAL is set to true, and this updated command information is
                logged to the OUTPUT directory.

                \param  agent Pointer to Agent object (for task addition)
                \param	cmd	Reference to event to run
                \param	nodename	Name of node
                \param	logdate_exec	Time of execution (for logging purposes)
            */
        int32_t CommandQueue::run_command(Agent* agent, Event& cmd, string node_name, double logdate_exec)
        {
            queue_changed = true;

            // utcexec and EVENT_FLAG_ACTUAL were set by due_commands()

            string outpath = data_type_path(node_name, "temp", "exec", logdate_exec, "out");
            printf("CMD: running command [%s] in [%s]\n", cmd.get_data().c_str(), outpath.c_str());
            // log to event file
            log_write(node_name, "exec", logdate_exec, "", "event", cmd.get_event_string().c_str(), "outgoing");
            int32_t iretn = agent->task_add(cmd.get_data(), node_name, cmd.timeout);
            if (iretn < 0)
            {
                printf("CMD: failed to run command [%s] for %d\n", cmd.get_data().c_str(), iretn);
                return iretn;
            }

            return 0;
        }

        //!	Traverse the entire queue of Events, and return those which qualify to run now.
        /*!
            See command_queue.h. Repeating conditional Events stay queued and are re-armed
            when their condition goes false; all other qualifying Events are removed.

            \param	cinfo	Node state the conditions are evaluated against
            \param	utc	Time of evaluation (MJD); 0 means the current wall-clock time
            \return	The Events to run, in queue order
        */
        vector<Event> CommandQueue::due_commands(cosmosstruc *cinfo, double utc)
        {
            vector<Event> due;
            if (utc == 0.)
            {
                utc = currentmjd(0.);
            }

            for (std::list<Event>::iterator ii = commands.begin(); ii != commands.end(); )
            {
                if (!ii->is_ready(utc))
                {
                    ++ii;
                    continue;
                }

                if (ii->is_conditional())
                {
                    if (!ii->condition_true(cinfo))
                    {
                        // Re-arm a repeating command once its condition goes false
                        ii->set_alreadyrun(false);
                        ++ii;
                        continue;
                    }
                    if (ii->is_repeat() && ii->is_alreadyrun())
                    {
                        ++ii;
                        continue;
                    }
                }

                // A solo command waits for a pass in which nothing else has run
                if (ii->is_solo() && !due.empty())
                {
                    ++ii;
                    continue;
                }

                ii->set_utcexec(utc);
                ii->set_actual();
                queue_changed = true;
                due.push_back(*ii);

                bool solo = ii->is_solo();
                if (ii->is_conditional() && ii->is_repeat())
                {
                    ii->set_alreadyrun(true);
                    ++ii;
                }
                else
                {
                    ii = commands.erase(ii);
                }

                // ...and nothing else runs after it
                if (solo)
                {
                    break;
                }
            }
            return due;
        }

        //!	Traverse the entire queue of Events, and run those which qualify.
        /*!

        Runs each %Event returned by due_commands(), as a command (task) or a request.

            \param	agent	Pointer to Agent object (for call to condition_true(..))
            \param	nodename	Name of the node
            \param	logdate_exec	Time of execution (for logging purposes)
            \param	utc	Time of evaluation (MJD); 0 means the current wall-clock time
        */
        int32_t CommandQueue::run_commands(Agent *agent, string node_name, double logdate_exec, double utc)
        {
            for (Event &cmd : due_commands(agent->cinfo, utc))
            {
                agent->cinfo->node.lastevent = cmd.name;
                agent->cinfo->node.lasteventutc = cmd.utcexec;
                if (cmd.type == EVENT_TYPE_COMMAND)
                {
                    run_command(agent, cmd, node_name, logdate_exec);
                    logdate_exec += 1./864000.;
                }
                else if (cmd.type == EVENT_TYPE_REQUEST)
                {
                    run_request(agent, cmd, node_name, logdate_exec);
                    logdate_exec += 1./864000.;
                }
                events.push_back(cmd);
                if (events.size() > 10)
                {
                    events.pop_front();
                }
            }
            return static_cast<int32_t>(commands.size());
        }

        //!	Save the queue of Events to a file
        /*!
            Commands are taken from the global command queue
            Command queue is sorted by utc after loading

            \param	temp_dir	Directory where the .queue file will be written
            \param	name	File where the .queue will be written
        */
        int32_t CommandQueue::save_commands(string temp_dir, string name)
        {
            if (!queue_changed)
            {
                return 0;
            }
            queue_changed = false;

            // Save previous queue
            rename((temp_dir+name).c_str(), (temp_dir+name+'.'+(utc2unixdate(currentmjd()))).c_str());

            // Open the outgoing file
            FILE *fd = fopen((temp_dir+name).c_str(), "w");
            if (fd != nullptr)
            {
                for (Event cmd: commands)
                {
                    fprintf(fd, "%s\n", cmd.get_event_string().c_str());
                }
                fclose(fd);
            }
            return static_cast<int32_t>(commands.size());
        }

        //!	Restore the queue of Events from a file
        /*!
            Commands are taken from the global command queue
            Command queue is sorted by utc after loading

            \param	temp_dir	Directory where the .queue file will be read from
            \param	name	File where the .queue will be read from
        */
        int32_t CommandQueue::restore_commands(string temp_dir, string name)
        {
            queue_changed = false;

            // Reload existing queue
            string infilepath = temp_dir + name;
            std::ifstream infile(infilepath.c_str());
            if(infile.is_open())
            {
                //file is open for reading commands
                string line;
                Event cmd;

                while(std::getline(infile,line))
                {
                    //cmd.set_command(line, agent);
                    cmd.set_command(line);

                    if(cmd.is_command())
                    {
                        add_command(cmd);
                    }
                }
                infile.close();
            }
            return static_cast<int32_t>(commands.size());
        }


        //!	Loads new commands from *.command files located in the incoming directory
        /*!
        // Commands are loaded into the global CommandQueue object (cmd_queue),
        // *.command files are removed, and the command list is sorted by utc.
            \param	incoming_dir	Directory where the .command files will be read from
        */
        int32_t CommandQueue::load_commands(string incoming_dir)
        {
            DIR *dir = nullptr;
            struct dirent *dir_entry = nullptr;

            // open the incoming directory
            if ((dir = opendir((char *)incoming_dir.c_str())) == nullptr)
            {
                return GENERAL_ERROR_OPEN;
            }

            // cycle through all the file names in the incoming directory
            while((dir_entry = readdir(dir)) != nullptr)
            {
                string filename = dir_entry->d_name;

                if (filename.find(".command") != string::npos)
                {

                    string infilepath = incoming_dir + filename;
                    std::ifstream infile(infilepath.c_str());
                    if(!infile.is_open())
                    {
                        //                        std::cout<<"unable to read file <"<<infilepath<<">"<<std::endl;
                        continue;
                    }

                    //file is open for reading commands
                    string line;
                    Event cmd;

                    while(getline(infile,line))
                    {
                        cmd.set_command(line);

                        if(cmd.is_command())
                        {
                            //                            std::cout << "Command added: " << cmd;
                            add_command(cmd);
                        }
                        //                        else
                        //                            std::cout<<"Not a command!"<<std::endl;
                    }
                    infile.close();

                    //remove the .command file from incoming directory
                    if(remove(infilepath.c_str()))	{
                        //                        std::cout<<"unable to delete file <"<<filename<<">"<<std::endl;
                        continue;
                    }

                    //                    std::cout<<"\nThe size of the command queue is: "<< get_command_size()<<std::endl;
                }
            }

            sort();

            closedir(dir);

            return 0;
        }
        ///	Remove **all** matching Event from the queue
        /**
            \param	c	Event to remove
            \return	The number of Events removed

            This function only removes events from the queue if the are exactly equal to the given Event.
        */
        int32_t CommandQueue::del_command(Event& c)
        {
            size_t prev_sz = commands.size();
            commands.remove_if([&c](const Event &e) { return c == e; });

            queue_changed = true;
            return static_cast<int32_t>(prev_sz - commands.size());

        }

        //! Remove Event from the queue based on position
        /*!
             \param  pos  Position of event to remove
             \return The number of Events removed

             This function removes events based on their queue position (0-indexed).
        */
        int32_t CommandQueue::del_command(int pos)
        {
            size_t prev_sz = commands.size();
            if (pos < 0 || static_cast<size_t>(pos) >= prev_sz)
            {
                return 0;
            }
            std::list<Event>::iterator b = commands.begin();

            std::advance(b, pos);
            commands.erase(b);
            queue_changed = true;
            return static_cast<int32_t>(prev_sz - commands.size());
        }

        int32_t CommandQueue::add_command(Event& c)
        {
            // Replace if it matches an existing command, otherwise add to queue
            for (std::list<Event>::iterator ii = commands.begin(); ii != commands.end(); ++ii) {
                if (c.get_name() == ii->get_name()) {
                    *ii = c;
                    queue_changed = true;
                    return 0;
                }
            }

            commands.push_back(c);
            queue_changed = true;
            return 1;
        }

        //!	Extraction operator
        /*!
            \param	out	Reference to ostream
            \param	cmdq	Reference to CommandQueue (JIMNOTE: should be const, ya?)
            \return	Reference to modified ostream

            Writes the given CommandQueue to the given output stream (in JSON format) and returns a reference to the modified ostream.

        */
        ::std::ostream& operator<<(::std::ostream& out, CommandQueue& cmdq)
        {
            for(std::list<Event>::iterator ii = cmdq.commands.begin(); ii != cmdq.commands.end(); ++ii)
                out << *ii << std::endl;
            return out;
        }
    } // end namespace Support
} // end namespace Cosmos
