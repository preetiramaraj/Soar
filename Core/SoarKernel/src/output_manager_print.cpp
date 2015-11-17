/*************************************************************************
 * PLEASE SEE THE FILE "COPYING" (INCLUDED WITH THIS SOFTWARE PACKAGE)
 * FOR LICENSE AND COPYRIGHT INFORMATION.
 *************************************************************************/

/*------------------------------------------------------------------
             output_manager_print.cpp

   @brief output_manager_print.cpp provides many functions to
   print Soar data structures.  Many were originally written
   for debugging purposes and are only fount in print commands.

------------------------------------------------------------------ */

#include "rhs.h"
#include "print.h"
#include "agent.h"
#include "instantiations.h"
#include "rete.h"
#include "reorder.h"
#include "rhs.h"
#include "rhs_functions.h"
#include "output_manager.h"
#include "output_manager_db.h"
#include "prefmem.h"
#include "wmem.h"
#include "soar_instance.h"
#include "test.h"
#include "variablization_manager.h"

#include <iostream>

#include <cstdarg>

void Output_Manager::printa_sf(agent* pSoarAgent, const char* format, ...)
{
    va_list args;
    std::string buf;

    va_start(args, format);
    vsnprint_sf(pSoarAgent, buf, format, args);
    va_end(args);
    printa(pSoarAgent, buf.c_str());
}

void Output_Manager::printa(agent* pSoarAgent, const char* msg)
{
    if (pSoarAgent)
    {
        if (!pSoarAgent->output_settings->print_enabled) return;
        if (pSoarAgent->output_settings->callback_mode)
        {
            soar_invoke_callbacks(pSoarAgent, PRINT_CALLBACK, static_cast<soar_call_data>(const_cast<char*>(msg)));
        }
        if (pSoarAgent->output_settings->stdout_mode)
        {
            fputs(msg, stdout);
        }

        update_printer_columns(pSoarAgent, msg);

        if (db_mode)
        {
            m_db->print_db(trace_msg, mode_info[No_Mode].prefix, msg);
        }
    }
}

void Output_Manager::printa_database(TraceMode mode, agent* pSoarAgent, MessageType msgType, const char* msg)
{
//    soar_module::sqlite_statement*   target_statement = NIL;

    if (((msgType == trace_msg) && mode_info[mode].enabled) ||
            ((msgType == debug_msg) && mode_info[mode].enabled))
    {
        m_db->print_db(msgType, mode_info[mode].prefix, msg);
    }
}

void Output_Manager::print_sf(const char* format, ...)
{
    if (m_defaultAgent)
    {
        va_list args;
        std::string buf;

        va_start(args, format);
        vsnprint_sf(m_defaultAgent, buf, format, args);
        va_end(args);
        printa(m_defaultAgent, buf.c_str());
    }
}

void Output_Manager::sprinta_sf(agent* thisAgent, std::string &destString, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprint_sf(thisAgent, destString, format, args);
    va_end(args);
    return;
}

/* Same as above but changes the destination pointer to point to the next character to write to.  Used
 * by other output manager functions to build up a string incrementally */

size_t Output_Manager::sprinta_sf_cstr(agent* thisAgent, char* dest, size_t dest_size, const char* format, ...)
{
    if (!dest_size) return 0;
    va_list args;
    std::string buf;

    va_start(args, format);
    vsnprint_sf(m_defaultAgent, buf, format, args);
    va_end(args);

    return om_strncpy(dest, buf.c_str(), dest_size, buf.length());
}

void Output_Manager::sprint_sf(std::string &destString, const char* format, ...)
{
    if (m_defaultAgent)
    {
        va_list args;
        va_start(args, format);
        vsnprint_sf(m_defaultAgent, destString, format, args);
        va_end(args);
    }
}

void Output_Manager::debug_print(TraceMode mode, const char* msg)
{
    if (!debug_mode_enabled(mode)) return;

    if (!m_defaultAgent)
    {
        std::cout << msg;
        return;
    }

    std::string buf;
    buffer_start_fresh_line(m_defaultAgent, buf);
    buf.append(mode_info[mode].prefix);
    buf.append(msg);
    printa(m_defaultAgent, buf.c_str());
}

void Output_Manager::debug_print_sf(TraceMode mode, const char* format, ...)
{
    if (!debug_mode_enabled(mode)) return;
    if (!m_defaultAgent)
    {
        std::cout << format;
        return;
    }

    va_list args;
    std::string buf;
    buffer_start_fresh_line(m_defaultAgent, buf);
    buf.append(mode_info[mode].prefix);

    va_start(args, format);
    vsnprint_sf(m_defaultAgent, buf, format, args);
    va_end(args);
    printa(m_defaultAgent, buf.c_str());
}

void Output_Manager::debug_print_sf_noprefix(TraceMode mode, const char* format, ...)
{
    if (!debug_mode_enabled(mode)) return;
    if (!m_defaultAgent)
    {
        std::cout << format;
        return;
    }

    va_list args;
    std::string buf;

    va_start(args, format);
    vsnprint_sf(m_defaultAgent, buf, format, args);
    va_end(args);

    printa(m_defaultAgent, buf.c_str());
}

void Output_Manager::debug_print_header(TraceMode mode, Print_Header_Type whichHeaders, const char* format, ...)
{
    if (!debug_mode_enabled(mode)) return;
    if (!m_defaultAgent)
    {
        std::cout << format;
        return;
    }

    std::string buf;
    buffer_start_fresh_line(m_defaultAgent, buf);
    if ((whichHeaders == PrintBoth) || (whichHeaders == PrintBefore))
        buf.append("=========================================================\n");
    buf.append(mode_info[mode].prefix);

    va_list args;

    va_start(args, format);
    vsnprint_sf(m_defaultAgent, buf, format, args);
    va_end(args);

    if ((whichHeaders == PrintBoth) || (whichHeaders == PrintAfter))
        buf.append("=========================================================\n");

    printa(m_defaultAgent, buf.c_str());
}

void Output_Manager::buffer_start_fresh_line(agent* thisAgent, std::string &destString)
{
    if (!thisAgent)
    {
        std::cout << std::endl;
        return;
    }

    if (destString.empty())
    {
        if ((global_printer_output_column != 1) || (thisAgent->output_settings->printer_output_column != 1))
        {
            destString.append("\n");
        }
    } else {
        if (destString.back() != '\n')
        {
            destString.append("\n");
        }
    }
}

void Output_Manager::debug_start_fresh_line(TraceMode mode)
{
    if (!debug_mode_enabled(mode)) return;

    if (!m_defaultAgent)
    {
        std::cout << std::endl;
        return;
    }

    if ((global_printer_output_column != 1) || (m_defaultAgent->output_settings->printer_output_column != 1))
    {
        printa(m_defaultAgent, "\n");
    }

}

void Output_Manager::vsnprint_sf(agent* thisAgent, std::string &destString, const char* format, va_list pargs)
{
    Symbol* sym;
    char ch=0;
	int i=0;
	size_t m;
    std::string sf = format;

    /* Apparently nested variadic calls need to have their argument list copied here.
     * If windows has issues with va_copy, might be able to just comment out that line
     * or use args = pargs.  Supposedly, way VC++ handles va_list doesn't need for it
     * to be copied. */
    va_list args;
    va_copy(args, pargs);

    m = sf.length();
    while (i<m)
    {
        ch = sf.at(i);
        //    /* --- copy anything up to the first "%" --- */
        //    if ((*format != '%') && (*format != 0))
        //    {
        //        char* first_p = strchr(format, '%');
        //        size_t first_len = 0;
        //        if (first_p)
        //        {
        //            first_len = (first_p - format);
        //        }
        //        if (first_len > 0)
        //        {
        //            char* new_format = const_cast<char*>(format);
        //            om_strncpy(&ch, &new_format, buffer_left, first_len);
        //            format = new_format;
        //        } else {
        //            size_t buffer_left_old = buffer_left;
        //            om_strcpy(&ch, format, buffer_left);
        //            format += (buffer_left_old - buffer_left - 1);
        //        }
        //    }
        //    if (*format == 0)
        //    {
        //        break;
        //    }
        if (ch == '%')
        {
            i++;
            if (i<m)
            {
                ch = sf.at(i);
                switch(ch)
                {
                    case 's':
                    {
                        char *ch2 = va_arg(args, char *);
                        if (ch2)
                        {
                            destString += ch2;
                        }
                    }
                    break;

                    case 'y':
                    {
                        sym = va_arg(args, Symbol*);
                        if (sym)
                        {
                            destString += sym->to_string(true);
                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'i':
                    {
                        destString += std::to_string(va_arg(args, int64_t));
                    }
                    break;

                    case 'u':
                    {
                        destString += std::to_string(va_arg(args, uint64_t));
                    }
                    break;

                    case 't':
                    {
                        test t = va_arg(args, test);
                        if (t)
                        {
                            test_to_string(t, destString);
                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'g':
                    {
                        test t = va_arg(args, test);
                        test ct = NULL;
                        if (t)
                        {
                            if (t->type != CONJUNCTIVE_TEST)
                            {
                                if (t->identity)
                                {
                                    test_to_string(t, destString);
                                    destString += " [o";
                                    destString += std::to_string(t->identity);
                                    destString += ' ';
                                    sym = thisAgent->variablizationManager->get_ovar_for_o_id(t->identity);
                                    if (sym) destString += sym->to_string(true); else destString += '#';
                                    destString += ']';
                                } else {
                                    test_to_string(t, destString);
                                    destString += " [o0]";
                                }
                            } else {
                                destString += "{ ";
                                for (cons *c = t->data.conjunct_list; c != NIL; c = c->rest)
                                {
                                    ct = static_cast<test>(c->first);
                                    assert(ct);
                                    if (t->identity)
                                    {
                                        test_to_string(t, destString);
                                        destString += " [o";
                                        destString += std::to_string(t->identity);
                                        destString += ' ';
                                        sym = thisAgent->variablizationManager->get_ovar_for_o_id(t->identity);
                                        if (sym) destString += sym->to_string(true); else destString += '#';
                                        destString += ']';
                                    } else {
                                        test_to_string(t, destString);
                                        destString += " [o0]";
                                    }
                                    destString += ' ';
                                }
                                destString += '}';
                            }
                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'l':
                    {
                        condition* lc = va_arg(args, condition*);
                        if (lc)
                        {
                            condition_to_string(thisAgent, lc, destString);
                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'a':
                    {
                        action* la = va_arg(args, action *);
                        if (la)
                        {
                            this->action_to_string(thisAgent, la, destString);

                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'n':
                    {
                        list* la = va_arg(args, list *);
                        if (la)
                        {
                            this->rhs_value_to_string(thisAgent, funcall_list_to_rhs_value(la), destString);

                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'r':
                    {
                        char* la = va_arg(args, char *);
                        if (la)
                        {
                            this->rhs_value_to_string(thisAgent, la, destString, NULL );

                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'p':
                    {
                        preference* lp = va_arg(args, preference *);
                        if (lp)
                        {
                            pref_to_string(thisAgent, lp, destString);

                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'w':
                    {
                        wme* lw = va_arg(args, wme *);
                        if (lw)
                        {
                            wme_to_string(thisAgent, lw, destString);
                        } else {
                            destString += '#';
                        }
                    }
                    break;

                    case 'd':
                    {
						int argument = va_arg(args, int);
                        destString += std::to_string(argument);
                    }
                    break;

                    case 'f':
                    {
                        if (thisAgent->output_settings->printer_output_column != 1)
                        {
                            destString += '\n';
                        }
                    }
                    break;

                    case '1':
                    {
                        condition* temp = va_arg(args, condition *);
                        if (temp)
                        {
                            condition_list_to_string(thisAgent, temp, destString);
                        }
                    }
                    break;

                    case '2':
                    {
                        action* temp = va_arg(args, action *);
                        if (temp)
                        {
                            action_list_to_string(thisAgent, temp, destString);
                        }
                    }
                    break;

                    case '3':
                    {
                        cons* temp = va_arg(args, cons*);
                        if (temp)
                        {
                            condition_cons_to_string(thisAgent, temp, destString);
                        }
                    }
                    break;

                    case '4':
                    {
                        cond_actions_to_string(thisAgent, va_arg(args, condition*), va_arg(args, action*), destString);
                    }
                    break;

                    case '5':
                    {
                        cond_prefs_to_string(thisAgent, va_arg(args, condition*), va_arg(args, preference*), destString);
                    }
                    break;

                    case '6':
                    {
                        cond_results_to_string(thisAgent, va_arg(args, condition*), va_arg(args, preference*), destString);
                    }
                    break;

                    case '7':
                    {
                        instantiation* temp = va_arg(args, instantiation*);
                        if (temp)
                        {
                            instantiation_to_string(thisAgent, temp, destString);
                        }
                    }
                    break;

                    case '8':
                    {
                        WM_to_string(thisAgent, destString);
                    }
                    break;

                    case 'c':
                    {
                        destString += static_cast<char>(va_arg(args, int));
                    }
                    break;

                    case '%': {
                        destString += '%';
                    }
                    break;

                    default:
                    {
                        destString += '%';
                        destString += ch;
                        //i = m; //get out of loop
                    }
                }
            }
        }
        else
        {
            destString += ch;
        }
        i++;
    }
    va_end(args);
}

