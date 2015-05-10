/*
 * variablization_manager_merge.cpp
 *
 *  Created on: Jul 25, 2013
 *      Author: mazzin
 */

#include "variablization_manager.h"
#include "agent.h"
#include "instantiations.h"
#include "prefmem.h"
#include "assert.h"
#include "test.h"
#include "print.h"
#include "debug.h"

void Variablization_Manager::remove_ungrounded_sti_tests(test* t, bool ignore_ungroundeds)
{
    assert(t);

    if ((*t)->type == EQUALITY_TEST)
    {
        /* Cache main equality test.  Clearly redundant for an equality test, but simplifies code. */
        (*t)->eq_test = (*t);
        return;
    }
    else if ((*t)->type == CONJUNCTIVE_TEST)
    {
        test tt, found_eq_test;
        ::list* c = (*t)->data.conjunct_list;
        while (c)
        {
            tt = static_cast<test>(c->first);

            // For all tests, check if referent is STI.  If so, it's ungrounded.  Delete.
            if (!ignore_ungroundeds && test_has_referent(tt) && (tt->data.referent->is_sti()))
            {
                dprint(DT_FIX_CONDITIONS, "Ungrounded STI found: %y\n", tt->data.referent);
                c = delete_test_from_conjunct(thisAgent, t, c);
                dprint(DT_FIX_CONDITIONS, "          ...after deletion: %t [%g]\n", (*t), (*t));
            }
            else if (tt->type == EQUALITY_TEST)
            {
                found_eq_test = tt;
                c = c->rest;
            }
            else
            {
                c = c->rest;
            }
        }

        /* -- We also cache the main equality test for each element in each condition
         *    since it's easy to do here. This allows us to avoid searching conjunctive
         *    tests repeatedly during merging. -- */
        (*t)->eq_test = found_eq_test;
        found_eq_test->eq_test = found_eq_test;
    }
}

o_id_update_info* Variablization_Manager::get_updated_o_id_info(uint64_t old_o_id)
{
    std::map< uint64_t, o_id_update_info* >::iterator iter = (*o_id_update_map).find(old_o_id);
    if (iter != (*o_id_update_map).end())
    {
        dprint(DT_VM_MAPS, "...found o%u(%y) in o_id_update_map for o%u\n",
            iter->second->o_id, iter->second->rule_symbol, old_o_id, get_ovar_for_o_id(old_o_id));

        return iter->second;
    } else {
        dprint(DT_VM_MAPS, "...did not find o%u(%y) in o_id_update_map.\n", old_o_id, get_ovar_for_o_id(old_o_id));
        print_o_id_update_map(DT_VM_MAPS);
    }
    return 0;
}

void Variablization_Manager::add_updated_o_id_info(uint64_t old_o_id, Symbol* new_ovar, uint64_t new_o_id, condition* pos_cond, WME_Field pos_cond_field)
{
    assert(get_updated_o_id_info(old_o_id) == 0);
    o_id_update_info* new_o_id_info = new o_id_update_info();
    new_o_id_info->rule_symbol = new_ovar;
    new_o_id_info->o_id = new_o_id;
    new_o_id_info->positive_cond = pos_cond;
    new_o_id_info->field = pos_cond_field;
    (*o_id_update_map)[old_o_id] = new_o_id_info;
}

void Variablization_Manager::create_consistent_identity_for_result_element(preference* result, uint64_t pI_id, WME_Field field)
{

    if (field == ID_ELEMENT)
    {
        create_consistent_identity_for_chunk(&(result->original_symbols.id), &(result->o_ids.id), pI_id, true);
    } else if (field == ATTR_ELEMENT) {
        create_consistent_identity_for_chunk(&(result->original_symbols.attr), &(result->o_ids.attr), pI_id, true);
    } else if (field == VALUE_ELEMENT) {
        create_consistent_identity_for_chunk(&(result->original_symbols.value), &(result->o_ids.value), pI_id, true);
    }
}

void Variablization_Manager::create_consistent_identity_for_chunk(Symbol** pOvar, uint64_t* pO_id, uint64_t pNew_i_id, bool pIsResult)
{
    uint64_t new_o_id = 0;
    Symbol* new_ovar = NULL;
    bool found_unique = false;

    if (!(*pO_id)) return;

    dprint(DT_OVAR_MAPPINGS, "update_o_id_for_new_instantiation called for %y o%u ", (*pOvar), (*pO_id));
    dprint_noprefix(DT_OVAR_MAPPINGS, "i%u %s\n", pNew_i_id, pIsResult ? "isResult" : "isNotResult");
    o_id_update_info* new_o_id_info = get_updated_o_id_info((*pO_id));
    if (new_o_id_info)
    {
        (*pO_id) = new_o_id_info->o_id;
        if ((*pOvar) != new_o_id_info->rule_symbol)
        {
            dprint(DT_OVAR_MAPPINGS, "...found existing variable update %y(o%u)\n", new_o_id_info->rule_symbol, new_o_id_info->o_id);
            symbol_remove_ref(thisAgent, (*pOvar));
            (*pOvar) = new_o_id_info->rule_symbol;
            symbol_add_ref(thisAgent, (*pOvar));
        }
    } else {
        if (pIsResult)
        {
            /* A RHS variable that was local to the substate, so it won't be variablized and doesn't need
             * these values.*/
            dprint(DT_OVAR_MAPPINGS, "...did not find update info for result %y(o%u).  Must be ungrounded.\n", (*pOvar), (*pO_id));
            if ((*pOvar))
            {
                symbol_remove_ref(thisAgent, (*pOvar));
            } else {
                /* MToDo|  Remove logic.  There should always be an oVar. */
                assert(false);
            }
            (*pOvar) = NULL;
            (*pO_id) = 0;
        } else {
            new_o_id = get_existing_o_id((*pOvar), pNew_i_id);
            if (new_o_id)
            {
                /* Ovar needs to be made unique.  This ovar has an existing o_id for new instantiation but no
                 * update info entry for the o_id.  That means that the previously seen case of this ovar
                 * had a different o_id. */
                std::string lVarName((*pOvar)->var->name+1);
                lVarName.erase(lVarName.length()-1);
                lVarName.append("-other");
                new_ovar = generate_new_variable(thisAgent, lVarName.c_str());
                //            dprint(DT_OVAR_MAPPINGS, "update_o_id_for_new_instantiation generated new variable %y from %s (%y ", new_ovar, lVarName.c_str(), ts);
                //            dprint_noprefix(DT_OVAR_MAPPINGS, "o%u i%u %s).\n", tu1, pNew_i_id, pIsResult ? "isResult" : "isNotResult");
                new_o_id = get_or_create_o_id(new_ovar, pNew_i_id);
                dprint(DT_OVAR_MAPPINGS, "...var name already used.  Generated new identity %y(%u) from varname root %s.\n", new_ovar, new_o_id, lVarName.c_str());
                add_updated_o_id_info((*pO_id), new_ovar, new_o_id);
                if ((*pOvar))
                {
                    symbol_remove_ref(thisAgent, (*pOvar));
                } else {
                    /* MToDo|  Remove logic.  There should always be an oVar. */
                    assert(false);
                }
                (*pOvar) = new_ovar;
            } else {
                /* First time this ovar has been encountered in the new instantiation.  So, create new
                 * o_id and add a new o_id_update_info entry for future tests that use the old o_id */
                new_o_id = get_or_create_o_id((*pOvar), pNew_i_id);
                dprint(DT_OVAR_MAPPINGS, "...var name not used.  Generated new identity for instantiation i%u %y(o%u).\n", pNew_i_id, (*pOvar), (*pO_id));
                add_updated_o_id_info((*pO_id), (*pOvar), new_o_id);
            }
            (*pO_id) = new_o_id;
        }
    }
}

void Variablization_Manager::fix_conditions(condition* top_cond, uint64_t pI_id, bool ignore_ungroundeds)
{
    dprint_header(DT_FIX_CONDITIONS, PrintBoth, "= Fixing conditions =\n");
    dprint_set_indents(DT_FIX_CONDITIONS, "          ");
    dprint_noprefix(DT_FIX_CONDITIONS, "%1", top_cond);
    dprint_clear_indents(DT_FIX_CONDITIONS);
    dprint_header(DT_FIX_CONDITIONS, PrintAfter, "");

    condition* next_cond, *last_cond = NULL;
    for (condition* cond = top_cond; cond;)
    {
        dprint(DT_FIX_CONDITIONS, "Finding redundancies in condition: %l\n", cond);
        next_cond = cond->next;
        if (cond->type != CONJUNCTIVE_NEGATION_CONDITION)
        {
            remove_ungrounded_sti_tests(&(cond->data.tests.id_test), ignore_ungroundeds);
            remove_ungrounded_sti_tests(&(cond->data.tests.attr_test), ignore_ungroundeds);
            remove_ungrounded_sti_tests(&(cond->data.tests.value_test), ignore_ungroundeds);
        }
        else
        {
            // Do we really need for NCCs?  I do think it might be possible to get
            // ungroundeds in NCCs
        }
        last_cond = cond;
        cond = next_cond;
    }

    dprint_header(DT_FIX_CONDITIONS, PrintBefore, "");
    dprint_set_indents(DT_FIX_CONDITIONS, "          ");
    dprint_noprefix(DT_FIX_CONDITIONS, "%1", top_cond);
    dprint_clear_indents(DT_FIX_CONDITIONS);
    dprint_header(DT_FIX_CONDITIONS, PrintBoth, "= Done fixing conditions =\n");
}

void Variablization_Manager::fix_results(preference* result, uint64_t pI_id)
{

    if (!result) return;

    dprint(DT_FIX_CONDITIONS, "Fixing result %p\n", result);
//    print_o_id_substitution_map(DT_FIX_CONDITIONS);
//    print_o_id_update_map(DT_FIX_CONDITIONS);
    print_o_id_tables(DT_FIX_CONDITIONS);

    if (pI_id)
    {
        if (result->original_symbols.id)
        {
            assert(result->original_symbols.id->is_variable());
            unify_identity_for_result_element(thisAgent, result, ID_ELEMENT);
            create_consistent_identity_for_result_element(result, pI_id, ID_ELEMENT);
        }
        if (result->original_symbols.attr)
        {
            assert(result->original_symbols.attr->is_variable());
            unify_identity_for_result_element(thisAgent, result, ATTR_ELEMENT);
            create_consistent_identity_for_result_element(result, pI_id, ATTR_ELEMENT);
        }
        if (result->original_symbols.value)
        {
            assert(result->original_symbols.value->is_variable());
            unify_identity_for_result_element(thisAgent, result, VALUE_ELEMENT);
            create_consistent_identity_for_result_element(result, pI_id, VALUE_ELEMENT);
        }
    }
    fix_results(result->next_result, pI_id);
    /* MToDo | Do we need to fix o_ids in clones too? */
}
