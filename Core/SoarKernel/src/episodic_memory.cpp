#include <portability.h>

/*************************************************************************
 * PLEASE SEE THE FILE "COPYING" (INCLUDED WITH THIS SOFTWARE PACKAGE)
 * FOR LICENSE AND COPYRIGHT INFORMATION. 
 *************************************************************************/

/*************************************************************************
 *
 *  file:  episodic_memory.cpp
 *
 * =======================================================================
 * Description  :  Various functions for Soar-EpMem
 * =======================================================================
 */

#include "episodic_memory.h"

#include <string>
#include <list>
#include <queue>
#include <map>
#include <algorithm>

#include "sqlite3.h"

using namespace soar_TraceNames;

// defined in symtab.cpp but not in symtab.h
//extern unsigned long compress( unsigned long h, short num_bits );
extern unsigned long hash_string( const char *s );

// epmem::params
// epmem::stats
// epmem::timers

// epmem::wmes
// epmem::prefs

// epmem::query
// epmem::transaction
// epmem::var

// epmem::rit

// epmem::clean
// epmem::init

// epmem::storage
// epmem::ncb
// epmem::cbr

// epmem::high


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Global Variables
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

const char *epmem_range_queries[2][2][3] =
{
	{
		{
			"SELECT e.start AS start FROM episodes e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM now e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM points e WHERE e.id=? ORDER BY e.start DESC"
		},
		{
			"SELECT e.end AS end FROM episodes e WHERE e.id=? ORDER BY e.end DESC",
			"SELECT ? AS end FROM now e WHERE e.id=?",
			"SELECT e.start AS end FROM points e WHERE e.id=? ORDER BY e.start DESC"
		}
	},
	{
		{
			"SELECT e.start AS start FROM episodes e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM now e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM points e WHERE e.id=? ORDER BY e.start DESC"
		},
		{
			"SELECT e.end AS end FROM episodes e WHERE e.id=? ORDER BY e.end DESC",
			"SELECT ? AS end FROM now e WHERE e.id=?",
			"SELECT e.start AS end FROM points e WHERE e.id=? ORDER BY e.start DESC"
		}
	},
};

const char *epmem_range_queries3[2][2][3] =
{
	{
		{
			"SELECT e.start AS start FROM search e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM now_search e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM points_search e WHERE e.id=? ORDER BY e.start DESC"
		},
		{
			"SELECT e.end AS end FROM search e WHERE e.id=? ORDER BY e.end DESC",
			"SELECT ? AS end FROM now_search e WHERE e.id=?",
			"SELECT e.start AS end FROM points_search e WHERE e.id=? ORDER BY e.start DESC"
		}
	},
	{
		{
			"SELECT e.start AS start FROM search e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM now_search e WHERE e.id=? ORDER BY e.start DESC",
			"SELECT e.start AS start FROM points_search e WHERE e.id=? ORDER BY e.start DESC"
		},
		{
			"SELECT e.end AS end FROM search e WHERE e.id=? ORDER BY e.end DESC",
			"SELECT ? AS end FROM now_search e WHERE e.id=?",
			"SELECT e.start AS end FROM points_search e WHERE e.id=? ORDER BY e.start DESC"
		}
	},
};

const long long epmem_rit_state_one[5] = { EPMEM_VAR_RIT_OFFSET_1, EPMEM_VAR_RIT_LEFTROOT_1, EPMEM_VAR_RIT_RIGHTROOT_1, EPMEM_VAR_RIT_MINSTEP_1, EPMEM_STMT_ONE_ADD_EPISODE };

const long long epmem_rit_state_three[2][5] =
{
	{ EPMEM_VAR_RIT_OFFSET_1, EPMEM_VAR_RIT_LEFTROOT_1, EPMEM_VAR_RIT_RIGHTROOT_1, EPMEM_VAR_RIT_MINSTEP_1, EPMEM_STMT_THREE_ADD_SEARCH },
	{ EPMEM_VAR_RIT_OFFSET_2, EPMEM_VAR_RIT_LEFTROOT_2, EPMEM_VAR_RIT_RIGHTROOT_2, EPMEM_VAR_RIT_MINSTEP_2, EPMEM_STMT_THREE_ADD_RECONSTRUCT }
};

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Parameter Functions (epmem::params)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// Cleans up memory for parameters
void epmem_clean_parameters( agent *my_agent )
{
	for ( int i=0; i<EPMEM_PARAMS; i++ )
	{
		if ( my_agent->epmem_params[ i ]->type == epmem_param_string )
			delete my_agent->epmem_params[ i ]->param->string_param.value;
		
		delete my_agent->epmem_params[ i ]->param;
		delete my_agent->epmem_params[ i ];
	}
}

// Add a new parameter (of desired type)
epmem_parameter *epmem_add_parameter( const char *name, double value, bool (*val_func)( double ) )
{
	epmem_parameter *newbie = new epmem_parameter;
	newbie->param = new epmem_parameter_union;
	newbie->param->number_param.value = value;
	newbie->param->number_param.val_func = val_func;
	newbie->type = epmem_param_number;
	newbie->name = name;
	
	return newbie;
}

epmem_parameter *epmem_add_parameter( const char *name, const long value, bool (*val_func)( const long ), const char *(*to_str)( long ), const long (*from_str)( const char * ) )
{
	epmem_parameter *newbie = new epmem_parameter;
	newbie->param = new epmem_parameter_union;
	newbie->param->constant_param.val_func = val_func;
	newbie->param->constant_param.to_str = to_str;
	newbie->param->constant_param.from_str = from_str;
	newbie->param->constant_param.value = value;
	newbie->type = epmem_param_constant;
	newbie->name = name;
	
	return newbie;
}

epmem_parameter *epmem_add_parameter( const char *name, const char *value, bool (*val_func)( const char * ) )
{
	epmem_parameter *newbie = new epmem_parameter;
	newbie->param = new epmem_parameter_union;
	newbie->param->string_param.value = new std::string( value );
	newbie->param->string_param.val_func = val_func;
	newbie->type = epmem_param_string;
	newbie->name = name;
	
	return newbie;
}

// Convert parameter name <=> constant
const char *epmem_convert_parameter( agent *my_agent, const long param )
{
	if ( ( param < 0 ) || ( param >= EPMEM_PARAMS ) )
		return NULL;

	return my_agent->epmem_params[ param ]->name;
}

const long epmem_convert_parameter( agent *my_agent, const char *name )
{
	for ( int i=0; i<EPMEM_PARAMS; i++ )
		if ( !strcmp( name, my_agent->epmem_params[ i ]->name ) )
			return i;

	return EPMEM_PARAMS;
}

// Determines if a parameter name/number is valid
bool epmem_valid_parameter( agent *my_agent, const char *name )
{
	return ( epmem_convert_parameter( my_agent, name ) != EPMEM_PARAMS );
}

bool epmem_valid_parameter( agent *my_agent, const long param )
{
	return ( epmem_convert_parameter( my_agent, param ) != NULL );
}

// Returns the parameter type
epmem_param_type epmem_get_parameter_type( agent *my_agent, const char *name )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return epmem_param_invalid;
	
	return my_agent->epmem_params[ param ]->type;
}

epmem_param_type epmem_get_parameter_type( agent *my_agent, const long param )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return epmem_param_invalid;

	return my_agent->epmem_params[ param ]->type;
}

// Get the parameter value
const long epmem_get_parameter( agent *my_agent, const char *name, const double /*test*/ )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return NULL;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->constant_param.value;
}

const char *epmem_get_parameter( agent *my_agent, const char *name, const char * /*test*/ )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return NULL;
	
	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
		return my_agent->epmem_params[ param ]->param->string_param.value->c_str();
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->constant_param.to_str( my_agent->epmem_params[ param ]->param->constant_param.value );
}

double epmem_get_parameter( agent *my_agent, const char *name )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return NULL;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_number )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->number_param.value;
}

//

const long epmem_get_parameter( agent *my_agent, const long param, const double /*test*/ )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return NULL;

	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->constant_param.value;
}

const char *epmem_get_parameter( agent *my_agent, const long param, const char * /*test*/ )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return NULL;
	
	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
		return my_agent->epmem_params[ param ]->param->string_param.value->c_str();
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->constant_param.to_str( my_agent->epmem_params[ param ]->param->constant_param.value );
}

double epmem_get_parameter( agent *my_agent, const long param )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return NULL;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_number )
		return NULL;
	
	return my_agent->epmem_params[ param ]->param->number_param.value;
}

// Returns if a value is valid for the parameter
bool epmem_valid_parameter_value( agent *my_agent, const char *name, double new_val )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_number )
		return false;
	
	return my_agent->epmem_params[ param ]->param->number_param.val_func( new_val );
}

bool epmem_valid_parameter_value( agent *my_agent, const char *name, const char *new_val )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
		return my_agent->epmem_params[ param ]->param->string_param.val_func( new_val );
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return false;
	
	return my_agent->epmem_params[ param ]->param->constant_param.val_func( my_agent->epmem_params[ param ]->param->constant_param.from_str( new_val ) );
}

bool epmem_valid_parameter_value( agent *my_agent, const char *name, const long new_val )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return false;
	
	return my_agent->epmem_params[ param ]->param->constant_param.val_func( new_val );
}

//

bool epmem_valid_parameter_value( agent *my_agent, const long param, double new_val )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_number )
		return false;
	
	return my_agent->epmem_params[ param ]->param->number_param.val_func( new_val );
}

bool epmem_valid_parameter_value( agent *my_agent, const long param, const char *new_val )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
		return my_agent->epmem_params[ param ]->param->string_param.val_func( new_val );
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return false;
	
	return my_agent->epmem_params[ param ]->param->constant_param.val_func( my_agent->epmem_params[ param ]->param->constant_param.from_str( new_val ) );
}

bool epmem_valid_parameter_value( agent *my_agent, const long param, const long new_val )
{
	if ( !epmem_valid_parameter( my_agent, param ) )
		return false;
	
	if ( epmem_get_parameter_type( my_agent, param ) != epmem_param_constant )
		return false;
	
	return my_agent->epmem_params[ param ]->param->constant_param.val_func( new_val );
}

// Returns true if a parameter is currently protected from modification
bool epmem_parameter_protected( agent *my_agent, const long param )
{
	return ( ( my_agent->epmem_db_status != -1 ) && ( param >= EPMEM_PARAM_DB ) && ( param <= EPMEM_PARAM_MODE ) );
}

// Set parameter value
bool epmem_set_parameter( agent *my_agent, const char *name, double new_val )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;

	if ( epmem_parameter_protected( my_agent, param ) )
		return false;

	// special case of commit needing conversion to int
	if ( param == EPMEM_PARAM_COMMIT )
		new_val = floor( new_val );
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;
	
	my_agent->epmem_params[ param ]->param->number_param.value = new_val;

	return true;
}

bool epmem_set_parameter( agent *my_agent, const char *name, const char *new_val )
{	
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;

	if ( epmem_parameter_protected( my_agent, param ) )
		return false;
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;

	// exclusions special case
	if ( param == EPMEM_PARAM_EXCLUSIONS )
	{
		std::string new_implode;
		
		// search through exclusions
		std::list<const char *>::iterator e_p = my_agent->epmem_exclusions->begin();
		while ( e_p != my_agent->epmem_exclusions->end() )
		{
			if ( strcmp( new_val, (*e_p) ) == 0 )
				break;			

			e_p++;
		}

		// remove if found
		if ( e_p != my_agent->epmem_exclusions->end() )
		{
			delete (*e_p);
			my_agent->epmem_exclusions->erase( e_p );

			// get new list
			e_p = my_agent->epmem_exclusions->begin();
			while ( e_p != my_agent->epmem_exclusions->end() )
			{
				new_implode.append( (*e_p) );
				
				e_p++;

				if ( e_p != my_agent->epmem_exclusions->end() )
					new_implode.append( ", " );
			}
		}
		// otherwise it's new
		else
		{			
			char *newbie = new char[ strlen( new_val ) + 1 ];
			strcpy( newbie, new_val );
			my_agent->epmem_exclusions->push_back( newbie );

			new_implode = (*my_agent->epmem_params[ param ]->param->string_param.value);
			if ( !new_implode.empty() )
				new_implode.append( ", " );

			new_implode.append( new_val );
		}
		
		// keep comma-separated list around
		(*my_agent->epmem_params[ param ]->param->string_param.value) = new_implode;
		return true;
	}

	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
	{
		(*my_agent->epmem_params[ param ]->param->string_param.value) = new_val;
		return true;
	}
	
	const long converted_val = my_agent->epmem_params[ param ]->param->constant_param.from_str( new_val );

	// learning special case
	if ( param == EPMEM_PARAM_LEARNING )
		set_sysparam( my_agent, EPMEM_ENABLED, converted_val );
	
	my_agent->epmem_params[ param ]->param->constant_param.value = converted_val;

	return true;
}

bool epmem_set_parameter( agent *my_agent, const char *name, const long new_val )
{
	const long param = epmem_convert_parameter( my_agent, name );
	if ( param == EPMEM_PARAMS )
		return false;

	if ( epmem_parameter_protected( my_agent, param ) )
		return false;
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;

	// learning special case
	if ( param == EPMEM_PARAM_LEARNING )
		set_sysparam( my_agent, EPMEM_ENABLED, new_val );
	
	my_agent->epmem_params[ param ]->param->constant_param.value = new_val;

	return true;
}

//

bool epmem_set_parameter( agent *my_agent, const long param, double new_val )
{
	if ( epmem_parameter_protected( my_agent, param ) )
		return false;

	// special case of commit needing conversion to int
	if ( param == EPMEM_PARAM_COMMIT )
		new_val = floor( new_val );
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;
	
	my_agent->epmem_params[ param ]->param->number_param.value = new_val;

	return true;
}

bool epmem_set_parameter( agent *my_agent, const long param, const char *new_val )
{
	if ( epmem_parameter_protected( my_agent, param ) )
		return false;
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;

	// exclusions special case
	if ( param == EPMEM_PARAM_EXCLUSIONS )
	{
		std::string new_implode;
		
		// search through exclusions
		std::list<const char *>::iterator e_p = my_agent->epmem_exclusions->begin();
		while ( e_p != my_agent->epmem_exclusions->end() )
		{
			if ( strcmp( new_val, (*e_p) ) == 0 )
				break;			

			e_p++;
		}

		// remove if found
		if ( e_p != my_agent->epmem_exclusions->end() )
		{
			delete (*e_p);
			my_agent->epmem_exclusions->erase( e_p );

			// get new list
			e_p = my_agent->epmem_exclusions->begin();
			while ( e_p != my_agent->epmem_exclusions->end() )
			{
				new_implode.append( (*e_p) );
				
				e_p++;

				if ( e_p != my_agent->epmem_exclusions->end() )
					new_implode.append( ", " );
			}
		}
		// otherwise it's new
		else
		{			
			char *newbie = new char[ strlen( new_val ) + 1 ];
			strcpy( newbie, new_val );
			my_agent->epmem_exclusions->push_back( newbie );

			new_implode = (*my_agent->epmem_params[ param ]->param->string_param.value);
			if ( !new_implode.empty() )
				new_implode.append( ", " );

			new_implode.append( new_val );
		}
		
		// keep comma-separated list around
		(*my_agent->epmem_params[ param ]->param->string_param.value) = new_implode;
		return true;
	}

	if ( epmem_get_parameter_type( my_agent, param ) == epmem_param_string )
	{
		(*my_agent->epmem_params[ param ]->param->string_param.value) = new_val;
		return true;
	}
	
	const long converted_val = my_agent->epmem_params[ param ]->param->constant_param.from_str( new_val );

	// learning special case
	if ( param == EPMEM_PARAM_LEARNING )
		set_sysparam( my_agent, EPMEM_ENABLED, converted_val );
	
	my_agent->epmem_params[ param ]->param->constant_param.value = converted_val;

	return true;
}

bool epmem_set_parameter( agent *my_agent, const long param, const long new_val )
{	
	if ( epmem_parameter_protected( my_agent, param ) )
		return false;
	
	if ( !epmem_valid_parameter_value( my_agent, param, new_val ) )
		return false;

	// learning special case
	if ( param == EPMEM_PARAM_LEARNING )
		set_sysparam( my_agent, EPMEM_ENABLED, new_val );
	
	my_agent->epmem_params[ param ]->param->constant_param.value = new_val;

	return true;
}

// learning parameter
bool epmem_validate_learning( const long new_val )
{
	return ( ( new_val == EPMEM_LEARNING_ON ) || ( new_val == EPMEM_LEARNING_OFF ) );
}

const char *epmem_convert_learning( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_LEARNING_ON:
			return_val = "on";
			break;
			
		case EPMEM_LEARNING_OFF:
			return_val = "off";
			break;
	}
	
	return return_val;
}

const long epmem_convert_learning( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "on" ) )
		return_val = EPMEM_LEARNING_ON;
	else if ( !strcmp( val, "off" ) )
		return_val = EPMEM_LEARNING_OFF;
	
	return return_val;
}

// database parameter
bool epmem_validate_database( const long new_val )
{
	return ( ( new_val == EPMEM_DB_MEM ) || ( new_val == EPMEM_DB_FILE ) );
}

const char *epmem_convert_database( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_DB_MEM:
			return_val = "memory";
			break;
			
		case EPMEM_DB_FILE:
			return_val = "file";
			break;
	}
	
	return return_val;
}

const long epmem_convert_database( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "memory" ) )
		return_val = EPMEM_DB_MEM;
	else if ( !strcmp( val, "file" ) )
		return_val = EPMEM_DB_FILE;
	
	return return_val;
}

// path parameter
bool epmem_validate_path( const char * /*new_val*/ )
{
	return true;
}

// mode parameter
bool epmem_validate_mode( const long new_val )
{
	return ( ( new_val >= EPMEM_MODE_ONE ) || ( new_val <= EPMEM_MODE_THREE ) );
}

const char *epmem_convert_mode( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_MODE_ONE:
			return_val = "one";
			break;
			
		case EPMEM_MODE_TWO:
			return_val = "two";
			break;

		case EPMEM_MODE_THREE:
			return_val = "three";
			break;
	}
	
	return return_val;
}

const long epmem_convert_mode( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "one" ) )
		return_val = EPMEM_MODE_ONE;
	else if ( !strcmp( val, "two" ) )
		return_val = EPMEM_MODE_TWO;
	else if ( !strcmp( val, "three" ) )
		return_val = EPMEM_MODE_THREE;
	
	return return_val;
}

// trigger parameter
bool epmem_validate_trigger( const long new_val )
{
	return ( ( new_val > 0 ) && ( new_val <= EPMEM_TRIGGER_DC ) );
}

const char *epmem_convert_trigger( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_TRIGGER_NONE:
			return_val = "none";
			break;

		case EPMEM_TRIGGER_OUTPUT:
			return_val = "output";
			break;
			
		case EPMEM_TRIGGER_DC:
			return_val = "dc";
			break;
	}
	
	return return_val;
}

const long epmem_convert_trigger( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "none" ) )
		return_val = EPMEM_TRIGGER_NONE;

	if ( !strcmp( val, "output" ) )
		return_val = EPMEM_TRIGGER_OUTPUT;
	
	if ( !strcmp( val, "dc" ) )
		return_val = EPMEM_TRIGGER_DC;
	
	return return_val;
}

// force parameter
bool epmem_validate_force( const long new_val )
{
	return ( ( new_val >= EPMEM_FORCE_REMEMBER ) || ( new_val <= EPMEM_FORCE_OFF ) );
}

const char *epmem_convert_force( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_FORCE_REMEMBER:
			return_val = "remember";
			break;
			
		case EPMEM_FORCE_IGNORE:
			return_val = "ignore";
			break;
			
		case EPMEM_FORCE_OFF:
			return_val = "off";
			break;
	}
	
	return return_val;
}

const long epmem_convert_force( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "remember" ) )
		return_val = EPMEM_FORCE_REMEMBER;
	else if ( !strcmp( val, "ignore" ) )
		return_val = EPMEM_FORCE_IGNORE;
	else if ( !strcmp( val, "off" ) )
		return_val = EPMEM_FORCE_OFF;
	
	return return_val;
}

// balance parameter
bool epmem_validate_balance( const double new_val )
{
	return ( ( new_val >= 0 ) && ( new_val <= 1 ) );
}

// exclusions parameter
bool epmem_validate_exclusions( const char * /*new_val*/ )
{
	return true;
}

// commit parameter
bool epmem_validate_commit( const double new_val )
{
	return ( new_val > 0 );
}

// timers parameter
bool epmem_validate_ext_timers( const long new_val )
{
	return ( ( new_val == EPMEM_TIMERS_ON ) || ( new_val == EPMEM_TIMERS_OFF ) );
}

const char *epmem_convert_ext_timers( const long val )
{
	const char *return_val = NULL;
	
	switch ( val )
	{
		case EPMEM_TIMERS_ON:
			return_val = "on";
			break;
			
		case EPMEM_TIMERS_OFF:
			return_val = "off";
			break;
	}
	
	return return_val;
}

const long epmem_convert_ext_timers( const char *val )
{
	long return_val = NULL;
	
	if ( !strcmp( val, "on" ) )
		return_val = EPMEM_TIMERS_ON;
	else if ( !strcmp( val, "off" ) )
		return_val = EPMEM_TIMERS_OFF;
	
	return return_val;
}

// shortcut function to system parameter
bool epmem_enabled( agent *my_agent )
{
	return ( my_agent->sysparams[ EPMEM_ENABLED ] == EPMEM_LEARNING_ON );
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Statistic Functions (epmem::stats)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// Cleans up memory for statistics
void epmem_clean_stats( agent *my_agent )
{
	for ( int i=0; i<EPMEM_STATS; i++ )
	  delete my_agent->epmem_stats[ i ];
}

// determines if a statistic is protected from resetting
bool epmem_stat_protected( agent *my_agent, const long stat )
{
	return ( ( my_agent->epmem_db_status != -1 ) && 
		     ( ( stat >= EPMEM_STAT_RIT_OFFSET_1 ) && ( stat <= EPMEM_STAT_RIT_MINSTEP_1 ) ) ||
			 ( ( stat >= EPMEM_STAT_RIT_OFFSET_2 ) && ( stat <= EPMEM_STAT_RIT_MINSTEP_2 ) ) ||
			 ( stat == EPMEM_STAT_TIME ) );
}

// resets unprotected statistics
void epmem_reset_stats( agent *my_agent )
{
	for ( int i=0; i<EPMEM_STATS; i++ )
		if ( !epmem_stat_protected( my_agent, i ) )
			my_agent->epmem_stats[ i ]->value = 0;
}

// add a statistic
epmem_stat *epmem_add_stat( const char *name )
{
	epmem_stat *newbie = new epmem_stat;
	newbie->name = name;
	newbie->value = 0;
	
	return newbie;
}

// convert between statistic name<=>number
const long epmem_convert_stat( agent *my_agent, const char *name )
{
	for ( int i=0; i<EPMEM_STATS; i++ )
		if ( !strcmp( name, my_agent->epmem_stats[ i ]->name ) )
			return i;

	return EPMEM_STATS;
}

const char *epmem_convert_stat( agent *my_agent, const long stat )
{
	if ( ( stat < 0 ) || ( stat >= EPMEM_STATS ) )
		return NULL;

	return my_agent->epmem_stats[ stat ]->name;
}

// return if a statistic name/number is valid
bool epmem_valid_stat( agent *my_agent, const char *name )
{
	return ( epmem_convert_stat( my_agent, name ) != EPMEM_STATS );
}

bool epmem_valid_stat( agent *my_agent, const long stat )
{
	return ( epmem_convert_stat( my_agent, stat ) != NULL );
}

// get a statistic value
long long epmem_get_stat( agent *my_agent, const char *name )
{
	const long stat = epmem_convert_stat( my_agent, name );
	if ( stat == EPMEM_STATS )
		return 0;
	
	if ( stat == EPMEM_STAT_MEM_USAGE )
		return sqlite3_memory_used();
	if ( stat == EPMEM_STAT_MEM_HIGH )
		return sqlite3_memory_highwater( false );

	return my_agent->epmem_stats[ stat ]->value;
}

long long epmem_get_stat( agent *my_agent, const long stat )
{
	if ( !epmem_valid_stat( my_agent, stat ) )
		return 0;
	
	if ( stat == EPMEM_STAT_MEM_USAGE )
		return sqlite3_memory_used();
	if ( stat == EPMEM_STAT_MEM_HIGH )
		return sqlite3_memory_highwater( false );

	return my_agent->epmem_stats[ stat ]->value;
}

// set a statistic value
bool epmem_set_stat( agent *my_agent, const char *name, long long new_val )
{
	const long stat = epmem_convert_stat( my_agent, name );
	if ( ( stat == EPMEM_STATS ) ||
		 ( stat == EPMEM_STAT_MEM_USAGE ) ||
		 ( stat == EPMEM_STAT_MEM_HIGH ) )
		return false;
	
	my_agent->epmem_stats[ stat ]->value = new_val;
	
	return true;
}

bool epmem_set_stat( agent *my_agent, const long stat, long long new_val )
{
	if ( !epmem_valid_stat( my_agent, stat ) )
		return false;
	
	if ( ( stat == EPMEM_STAT_MEM_USAGE ) ||
		 ( stat == EPMEM_STAT_MEM_HIGH ) )
		return false;
	
	my_agent->epmem_stats[ stat ]->value = new_val;
	
	return true;
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Timer Functions (epmem::timers)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// Cleans up memory for timers
void epmem_clean_timers( agent *my_agent )
{
	for ( int i=0; i<EPMEM_TIMERS; i++ )
		delete my_agent->epmem_timers[ i ];
}

// resets all timers
void epmem_reset_timers( agent *my_agent )
{
	for ( int i=0; i<EPMEM_TIMERS; i++ )
	{
		reset_timer( &my_agent->epmem_timers[ i ]->start_timer );
		reset_timer( &my_agent->epmem_timers[ i ]->total_timer );
	}
}

// adds/initializes a timer
epmem_timer *epmem_add_timer( const char *name )
{
	// new timer entry
	epmem_timer *newbie = new epmem_timer;
	newbie->name = name;

	reset_timer( &newbie->start_timer );
	reset_timer( &newbie->total_timer );
	
	return newbie;
}

// convert a timer name<=>number
const long epmem_convert_timer( agent *my_agent, const char *name )
{
	for ( int i=0; i<EPMEM_TIMERS; i++ )
		if ( !strcmp( name, my_agent->epmem_timers[ i ]->name ) )
			return i;

	return EPMEM_TIMERS;
}

const char *epmem_convert_timer( agent *my_agent, const long timer )
{
	if ( ( timer < 0 ) || ( timer >= EPMEM_TIMERS ) )
		return NULL;

	return my_agent->epmem_timers[ timer ]->name;
}

// determines if the timer name/number is valid
bool epmem_valid_timer( agent *my_agent, const char *name )
{
	return ( epmem_convert_timer( my_agent, name ) != EPMEM_TIMERS );
}

bool epmem_valid_timer( agent *my_agent, const long timer )
{
	return ( epmem_convert_timer( my_agent, timer ) != NULL );
}

// returns the current value of the timer
double epmem_get_timer_value( agent *my_agent, const char *name )
{
	const long timer = epmem_convert_timer( my_agent, name );
	if ( timer == EPMEM_TIMERS )
		return 0.0;

	return timer_value( &my_agent->epmem_timers[ timer ]->total_timer );
}

double epmem_get_timer_value( agent *my_agent, const long timer )
{
	if ( !epmem_valid_timer( my_agent, timer ) )
		return 0.0;

	return timer_value( &my_agent->epmem_timers[ timer ]->total_timer );
}

// returns the timer name
const char *epmem_get_timer_name( agent *my_agent, const char *name )
{
	const long timer = epmem_convert_timer( my_agent, name );
	if ( timer == EPMEM_TIMERS )
		return 0;

	return my_agent->epmem_timers[ timer ]->name;
}

const char *epmem_get_timer_name( agent *my_agent, const long timer )
{
	if ( !epmem_valid_timer( my_agent, timer ) )
		return 0;

	return my_agent->epmem_timers[ timer ]->name;
}

// starts a timer
void epmem_start_timer( agent *my_agent, const long timer )
{
	if ( epmem_valid_timer( my_agent, timer ) && ( epmem_get_parameter( my_agent, EPMEM_PARAM_TIMERS, EPMEM_RETURN_LONG ) == EPMEM_TIMERS_ON ) )
	{
		start_timer( my_agent, &my_agent->epmem_timers[ timer ]->start_timer );
	}
}

// stops a timer
void epmem_stop_timer( agent *my_agent, const long timer )
{
	if ( epmem_valid_timer( my_agent, timer ) && ( epmem_get_parameter( my_agent, EPMEM_PARAM_TIMERS, EPMEM_RETURN_LONG ) == EPMEM_TIMERS_ON ) )
	{
		stop_timer( my_agent, &my_agent->epmem_timers[ timer ]->start_timer, &my_agent->epmem_timers[ timer ]->total_timer );
	}
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// WME Functions (epmem::wmes)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

/***************************************************************************
 * Function     : epmem_get_augs_of_id
 * Author		: Andy Nuxoll
 * Notes		: This routine works just like the one defined in utilities.h.
 *				  Except this one does not use C++ templates because I have an
 *				  irrational dislike for them borne from the years when the STL
 *				  highly un-portable.  I'm told this is no longer true but I'm still
 *				  bitter.
 **************************************************************************/
wme **epmem_get_augs_of_id( agent* my_agent, Symbol * id, tc_number tc, int *num_attr )
{
	slot *s;
	wme *w;
	wme **list;
	int list_position;
	int n = 0;

	// augs only exist for identifiers
	if ( id->common.symbol_type != IDENTIFIER_SYMBOL_TYPE )
		return NULL;

	// don't want to get into a loop
	if ( id->id.tc_num == tc )
		return NULL;
	id->id.tc_num = tc;

	// first count number of augs, required for later allocation
	for ( w = id->id.impasse_wmes; w != NIL; w = w->next )
		n++;
	for ( w = id->id.input_wmes; w != NIL; w = w->next )
		n++;
	for ( s = id->id.slots; s != NIL; s = s->next ) 
	{
		for ( w = s->wmes; w != NIL; w = w->next )
			n++;
		for ( w = s->acceptable_preference_wmes; w != NIL; w = w->next )
			n++;
	}
	
	// allocate the list, note the size
	list = static_cast<wme**>(allocate_memory( my_agent, n * sizeof(wme *), MISCELLANEOUS_MEM_USAGE));
	( *num_attr ) = n;

	list_position = 0;
	for ( w = id->id.impasse_wmes; w != NIL; w = w->next )
       list[ list_position++ ] = w;
	for ( w = id->id.input_wmes; w != NIL; w = w->next )
		list[ list_position++ ] = w;
	for ( s = id->id.slots; s != NIL; s = s->next ) 
	{
		for ( w = s->wmes; w != NIL; w = w->next )
           list[ list_position++ ] = w;
		for ( w = s->acceptable_preference_wmes; w != NIL; w = w->next )
			list[ list_position++ ] = w;
	}
	
	return list;
}

/***************************************************************************
 * Function     : epmem_wme_has_value
 * Author		: Andy Nuxoll
 * Notes		: This routine returns TRUE if the given WMEs attribute 
 *                and value are both symbols and have the names given.  
 *                If either of the given names are NULL then they are 
 *                assumed to be a match (i.e., a wildcard).  Thus passing 
 *                NULL for both attr_name and value_name will always yield 
 *                a TRUE result.
 **************************************************************************/
bool epmem_wme_has_value( wme *w, char *attr_name, char *value_name )
{
	if ( w == NULL )
		return false;

	if ( attr_name != NULL )
	{
		if ( w->attr->common.symbol_type != SYM_CONSTANT_SYMBOL_TYPE )
			return false;

		if ( strcmp( w->attr->sc.name, attr_name ) != 0 )
			return false;
	}

    if ( value_name != NULL )
	{
		if ( w->value->common.symbol_type != SYM_CONSTANT_SYMBOL_TYPE )
			return false;

		if ( strcmp( w->attr->sc.name, value_name ) != 0 )
			return false;
	}

	return true;
}

/***************************************************************************
 * Function     : epmem_get_aug_of_id
 * Author		: Andy Nuxoll
 * Notes		: This routine examines a symbol for an augmentation that
 *				  has the given attribute and value and returns it.  See
 *                epmem_wme_has_value() for info on how the correct wme is 
 *                matched to the given strings.
 **************************************************************************/
wme *epmem_get_aug_of_id( agent *my_agent, Symbol *sym, char *attr_name, char *value_name )
{
	wme **wmes;
	int len = 0;
	wme *return_val = NULL;

	wmes = epmem_get_augs_of_id( my_agent, sym, get_new_tc_number( my_agent ), &len );
	if ( wmes == NULL )
		return return_val;

	for ( int i=0; i<len; i++ )
	{
		if ( epmem_wme_has_value( wmes[ i ], attr_name, value_name ) )
		{
			return_val = wmes[ i ];
			break;
		}
	}

	free_memory( my_agent, wmes, MISCELLANEOUS_MEM_USAGE );

	return return_val;
}

/***************************************************************************
 * Function     : epmem_hash_wme
 * Author		: Andy Nuxoll
 * Notes		: Creates a hash value for a WME.  This is used to find the
 *				  corresponding wmetree node in a hash table.
 **************************************************************************/
unsigned long epmem_hash_wme( wme *w )
{
	unsigned long hash_value;
	std::string *temp;
	
	// Generate a hash value for the WME's attr and value
	hash_value = hash_string( w->attr->sc.name );
	
	switch( w->value->common.symbol_type )
	{
		case SYM_CONSTANT_SYMBOL_TYPE:
			hash_value += hash_string( w->value->sc.name );
			break;
            
		case INT_CONSTANT_SYMBOL_TYPE:
			temp = to_string( w->value->ic.value );
			hash_value += hash_string( temp->c_str() );
			delete temp;
			break;
		
		case FLOAT_CONSTANT_SYMBOL_TYPE:
			temp = to_string( w->value->fc.value );
			hash_value += hash_string( temp->c_str() );
			delete temp;			
			break;
	}
	
	return hash_value;
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Preference Functions (epmem::prefs)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

/***************************************************************************
 * Function     : epmem_make_fake_preference
 * Author		: Andy Nuxoll
 * Notes		: This function adds a fake preference to a WME so that 
 *                it will not be added to the goal dependency set of the 
 *                state it is attached to.  This is used to prevents the 
 *                GDS from removing a state whenever a epmem is retrieved 
 *                that is attached to it.
 *
 *                (The bulk of the content of this function is taken from
 *                 make_fake_preference_for_goal_item() in decide.c)
 **************************************************************************/
preference *epmem_make_fake_preference( agent *my_agent, Symbol *state, wme *w )
{
	// if we are on the top state, don't make the preference
	if ( state->id.epmem_info->ss_wme == NULL )
		return NIL;
	
	// make fake preference
	preference *pref = make_preference( my_agent, ACCEPTABLE_PREFERENCE_TYPE, w->id, w->attr, w->value, NIL );
	pref->o_supported = TRUE;
	symbol_add_ref( pref->id );
	symbol_add_ref( pref->attr );
	symbol_add_ref( pref->value );

	// add preference to goal list
	insert_at_head_of_dll( state->id.preferences_from_goal, pref, all_of_goal_next, all_of_goal_prev );
	pref->on_goal_list = TRUE;

	// add reference
	preference_add_ref( pref );

	// make fake instantiation
	instantiation *inst;
	allocate_with_pool( my_agent, &( my_agent->instantiation_pool ), &inst );
	pref->inst = inst;
	pref->inst_next = pref->inst_prev = NULL;
	inst->preferences_generated = pref;
	inst->prod = NULL;
	inst->next = inst->prev = NULL;
	inst->rete_token = NULL;
	inst->rete_wme = NULL;
	inst->match_goal = state;
	inst->match_goal_level = state->id.level;
	inst->okay_to_variablize = TRUE;
	inst->backtrace_number = 0;
	inst->in_ms = FALSE;

	// create a condition for each cue WME (superstate if no cue)
	bool no_cue = state->id.epmem_info->cue_wmes->empty();
	condition *cond = NULL;
	condition *prev_cond = NULL;
	if ( no_cue )
		state->id.epmem_info->cue_wmes->push_back( state->id.epmem_info->ss_wme );
	{
		std::list<wme *>::iterator p = state->id.epmem_info->cue_wmes->begin();

		while ( p != state->id.epmem_info->cue_wmes->end() )
		{
			// construct the condition
			allocate_with_pool( my_agent, &( my_agent->condition_pool ), &cond );
			cond->type = POSITIVE_CONDITION;
			cond->prev = prev_cond;
			cond->next = NULL;
			if ( prev_cond != NULL )
			{
				prev_cond->next = cond;
			}
			else
			{
				inst->top_of_instantiated_conditions = cond;
				inst->bottom_of_instantiated_conditions = cond;
				inst->nots = NULL;
			}
			cond->data.tests.id_test = make_equality_test( (*p)->id );
			cond->data.tests.attr_test = make_equality_test( (*p)->attr );
			cond->data.tests.value_test = make_equality_test( (*p)->value );
			cond->test_for_acceptable_preference = TRUE;
			cond->bt.wme_ = (*p);
			wme_add_ref( (*p) );
			cond->bt.level = (*p)->id->id.level;
			cond->bt.trace = (*p)->preference;
			if ( cond->bt.trace )
				preference_add_ref( cond->bt.trace );
			cond->bt.prohibits = NULL;

			prev_cond = cond;

			p++;
		}
	}
	if ( no_cue )
		state->id.epmem_info->cue_wmes->clear();  
    
    return pref;
}

/***************************************************************************
 * Function     : epmem_remove_fake_preference
 * Author		: Andy Nuxoll
 * Notes		: This function removes a fake preference on a WME 
 *                created by epmem_make_fake_preference().  While it's
 *                a one-line function I thought it was important to
 *                create so it would be clear what's going on in this
 *                case.
 **************************************************************************/
void epmem_remove_fake_preference( agent *my_agent, wme *w )
{
	if ( w->preference )
		preference_remove_ref( my_agent, w->preference );
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Query Functions (epmem::query)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// starts/stops a timer around query execution
int epmem_exec_query( agent *my_agent, sqlite3_stmt *stmt, const long timer )
{
	int return_val;	
	
	epmem_start_timer( my_agent, timer );
	return_val = sqlite3_step( stmt );
	epmem_stop_timer( my_agent, timer );

	return return_val;
}

// performs timed range queries
int epmem_exec_range_query( agent *my_agent, epmem_range_query *stmt )
{
	return epmem_exec_query( my_agent, stmt->stmt, stmt->timer );
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Transaction Functions (epmem::transaction)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// returns true if currently in a transaction
// according to the value of commit
bool epmem_in_transaction( agent *my_agent )
{
	if ( my_agent->epmem_db_status == -1 )
		return false;
	
	return ( (long long) epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME ) % (long long) epmem_get_parameter( my_agent, EPMEM_PARAM_COMMIT ) );
}

// starts a transaction
void epmem_transaction_begin( agent *my_agent )
{
	if ( my_agent->epmem_db_status != -1 )
	{
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_BEGIN ] );
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_BEGIN ] );
	}
}

// ends the current transaction
void epmem_transaction_end( agent *my_agent, bool commit )
{
	if ( my_agent->epmem_db_status != -1 )
	{
		unsigned long end_method = ( ( commit )?( EPMEM_STMT_COMMIT ):( EPMEM_STMT_ROLLBACK ) );
	
		sqlite3_step( my_agent->epmem_statements[ end_method ] );
		sqlite3_reset( my_agent->epmem_statements[ end_method ] );
	}
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Variable Functions (epmem::var)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// gets an epmem variable
bool epmem_get_variable( agent *my_agent, long long variable_id, long long &variable_value )
{
	int status;
	
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_VAR_GET ], 1, variable_id );
	status = sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_VAR_GET ] );
	
	if ( status == SQLITE_ROW )
		variable_value = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_VAR_GET ], 0 );

	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_VAR_GET ] );

	return ( status == SQLITE_ROW );
}

// sets an epmem variable
void epmem_set_variable( agent *my_agent, long long variable_id, long long variable_value )
{
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_VAR_SET ], 1, variable_id );
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_VAR_SET ], 2, variable_value );
	sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_VAR_SET ] );
	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_VAR_SET ] );
}

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// RIT Functions (epmem::rit)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

long long epmem_rit_fork_node( agent *my_agent, epmem_time_id lower, epmem_time_id upper, bool bounds_offset, long long *step_return, const long long *rit_state )
{	
	if ( !bounds_offset )
	{
		long long offset = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_OFFSET ] );

		lower = ( lower - offset );
		upper = ( upper - offset );
	}
	
	// descend the tree down to the fork node
	long long node = EPMEM_RIT_ROOT;
	if ( upper < EPMEM_RIT_ROOT )
		node = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_LEFTROOT ] );
	else if ( lower > EPMEM_RIT_ROOT )
		node = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_RIGHTROOT ] );

	long long step;	
	for ( step = ( ( ( node >= 0 )?( node ):( -1 * node ) ) / 2 ); step >= 1; step /= 2 )
	{
		if ( upper < node )
			node -= step;
		else if ( node < lower )
			node += step;
		else
			break;
	}

	if ( step_return != NULL )
		(*step_return) = step;

	return node;
}

void epmem_rit_clear_left_right( agent *my_agent )
{
	sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_LEFT ] );
	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_LEFT ] );
	
	sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_RIGHT ] );
	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_RIGHT ] );
}

void epmem_rit_add_left( agent *my_agent, epmem_time_id min, epmem_time_id max )
{
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_LEFT ], 1, min );
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_LEFT ], 2, max );
	
	sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_LEFT ] );
	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_LEFT ] );
}

void epmem_rit_add_right( agent *my_agent, epmem_time_id id )
{
	sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_RIGHT ], 1, id );	

	sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_RIGHT ] );
	sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_RIGHT ] );
}

void epmem_rit_prep_left_right( agent *my_agent, epmem_time_id lower, epmem_time_id upper, const long long *rit_state )
{
	long long offset = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_OFFSET ] );
	long long node, step;
	long long left_node, left_step;
	long long right_node, right_step;

	lower = ( lower - offset );
	upper = ( upper - offset );

	// auto add good range
	epmem_rit_add_left( my_agent, lower, upper );

	// go to fork
	node = EPMEM_RIT_ROOT;
	step = 0;
	if ( ( lower > node ) || (upper < node ) )
	{
		if ( lower > node )
		{
			node = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_RIGHTROOT ] );
			epmem_rit_add_left( my_agent, EPMEM_RIT_ROOT, EPMEM_RIT_ROOT );
		}
		else
		{
			node = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_LEFTROOT ] );
			epmem_rit_add_right( my_agent, EPMEM_RIT_ROOT );
		}

		for ( step = ( ( ( node >= 0 )?( node ):( -1 * node ) ) / 2 ); step >= 1; step /= 2 )
		{
			if ( lower > node )
			{
				epmem_rit_add_left( my_agent, node, node );
				node += step;
			}
			else if ( upper < node )
			{
				epmem_rit_add_right( my_agent, node );
				node -= step;
			}
			else
				break;
		}
	}

	// go left
	left_node = node - step;
	for ( left_step = ( step / 2 ); left_step >= 1; left_step /= 2 )
	{
		if ( lower == left_node )
			break;
		else if ( lower > left_node )
		{
			epmem_rit_add_left( my_agent, left_node, left_node );
			left_node += left_step;
		}
		else
			left_node -= left_step;
	}

	// go right
	right_node = node + step;
	for ( right_step = ( step / 2 ); right_step >= 1; right_step /= 2 )
	{
		if ( upper == right_node )
			break;
		else if ( upper < right_node )
		{
			epmem_rit_add_right( my_agent, right_node );
			right_node -= right_step;
		}
		else
			right_node += right_step;
	}
}

// inserts an interval in the RIT
void epmem_rit_insert_interval( agent *my_agent, epmem_time_id lower, epmem_time_id upper, epmem_node_id id, const long long *rit_state )
{
	// initialize offset
	long long offset = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_OFFSET ] );
	if ( offset == -1 )
	{
		offset = lower;
		
		// update database
		epmem_set_variable( my_agent, rit_state[ EPMEM_RIT_STATE_OFFSET ], offset );		

		// update stat
		epmem_set_stat( my_agent, rit_state[ EPMEM_RIT_STATE_OFFSET ], offset );
	}

	// get node
	long long node;	
	{
		long long left_root = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_LEFTROOT ] );
		long long right_root = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_RIGHTROOT ] );
		long long min_step = epmem_get_stat( my_agent, rit_state[ EPMEM_RIT_STATE_MINSTEP ] );		

		// shift interval
		epmem_time_id l = ( lower - offset );
		epmem_time_id u = ( upper - offset );

		// update left_root
		if ( ( u < EPMEM_RIT_ROOT ) && ( l <= ( 2 * left_root ) ) )
		{
			left_root = (long long) pow( -2, floor( log( (double) -l ) / EPMEM_LN_2 ) );

			// update database
			epmem_set_variable( my_agent, rit_state[ EPMEM_RIT_STATE_LEFTROOT ], left_root );			

			// update stat
			epmem_set_stat( my_agent, rit_state[ EPMEM_RIT_STATE_LEFTROOT ], left_root );
		}

		// update right_root
		if ( ( l > EPMEM_RIT_ROOT ) && ( u >= ( 2 * right_root ) ) )
		{
			right_root = (long long) pow( 2, floor( log( (double) u ) / EPMEM_LN_2 ) );

			// update database
			epmem_set_variable( my_agent, rit_state[ EPMEM_RIT_STATE_RIGHTROOT ], right_root );			

			// update stat
			epmem_set_stat( my_agent, rit_state[ EPMEM_RIT_STATE_RIGHTROOT ], right_root );
		}

		// update min_step				
		long long step;
		node = epmem_rit_fork_node( my_agent, l, u, true, &step, rit_state );

		if ( ( node != EPMEM_RIT_ROOT ) && ( step < min_step ) )
		{
			min_step = step;

			// update database
			epmem_set_variable( my_agent, rit_state[ EPMEM_RIT_STATE_MINSTEP ], min_step );			

			// update stat
			epmem_set_stat( my_agent, rit_state[ EPMEM_RIT_STATE_MINSTEP ], min_step );
		}		
	}

	// perform insert
	// ( node, start, end, id )
	sqlite3_bind_int64( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ], 1, node );
	sqlite3_bind_int64( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ], 2, lower );
	sqlite3_bind_int64( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ], 3, upper );
	sqlite3_bind_int64( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ], 4, id );
	sqlite3_step( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ] );
	sqlite3_reset( my_agent->epmem_statements[ rit_state[ EPMEM_RIT_STATE_ADD ] ] );
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Clean-Up Functions (epmem::clean)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// performs cleanup operations when the database
// needs to be closed (end soar, close db, etc)
void epmem_end( agent *my_agent )
{
	if ( my_agent->epmem_db_status != -1 )
	{	
		if ( epmem_in_transaction( my_agent ) )
			epmem_transaction_end( my_agent, true );
		
		// perform cleanup as necessary
		const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );		
		
		for ( int i=0; i<EPMEM_MAX_STATEMENTS; i++ )
		{
			if ( my_agent->epmem_statements[ i ] != NULL )
			{
				sqlite3_finalize( my_agent->epmem_statements[ i ] );
				my_agent->epmem_statements[ i ] = NULL;
			}
		}

		sqlite3_close( my_agent->epmem_db );
		
		my_agent->epmem_db = NULL;
		my_agent->epmem_db_status = -1;
	}

	std::list<const char *>::iterator e_p = my_agent->epmem_exclusions->begin();
	while ( e_p != my_agent->epmem_exclusions->end() )
	{
		delete (*e_p);
		e_p++;
	}
}

// performs cleanup of the current state retrieval
void epmem_clear_result( agent *my_agent, Symbol *state )
{	
	while ( !state->id.epmem_info->epmem_wmes->empty() )
	{		
		epmem_remove_fake_preference( my_agent, state->id.epmem_info->epmem_wmes->top() );
		remove_input_wme( my_agent, state->id.epmem_info->epmem_wmes->top() );		
		state->id.epmem_info->epmem_wmes->pop();
	}	
}

// performs cleanup when a state is removed
void epmem_reset( agent *my_agent, Symbol *state )
{
	if ( state == NULL )
		state = my_agent->top_goal;

	while( state )
	{
		epmem_data *data = state->id.epmem_info;
				
		data->last_ol_time = 0;
		data->last_ol_count = 0;

		data->last_cmd_time = 0;
		data->last_cmd_count = 0;

		data->last_memory = EPMEM_MEMID_NONE;

		data->cue_wmes->clear();

		// clear off any result stuff (takes care of epmem_wmes)
		epmem_clear_result( my_agent, state );

		// remove fake preferences
		epmem_remove_fake_preference( my_agent, state->id.epmem_wme );
		epmem_remove_fake_preference( my_agent, state->id.epmem_cmd_wme );
		epmem_remove_fake_preference( my_agent, state->id.epmem_result_wme );
		
		state = state->id.lower_goal;
	}
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Initialization Functions (epmem::init)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

void epmem_init_db( agent *my_agent )
{
	if ( my_agent->epmem_db_status != -1 )
		return;
	
	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_INIT );
	////////////////////////////////////////////////////////////////////////////

	const char *db_path;
	if ( epmem_get_parameter( my_agent, EPMEM_PARAM_DB, EPMEM_RETURN_LONG ) == EPMEM_DB_MEM )
		db_path = ":memory:";
	else
		db_path = epmem_get_parameter( my_agent, EPMEM_PARAM_PATH, EPMEM_RETURN_STRING );
	
	// attempt connection
	my_agent->epmem_db_status = sqlite3_open_v2( db_path, &(my_agent->epmem_db), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL );
	if ( my_agent->epmem_db_status )
	{
		char buf[256];
		SNPRINTF( buf, 254, "DB ERROR: %s", sqlite3_errmsg( my_agent->epmem_db ) );
		
		print( my_agent, buf );
				
		xml_generate_warning( my_agent, buf );
	}
	else
	{
		const char *tail;
		sqlite3_stmt *create;
		epmem_time_id time_max;		

		// point stuff
		epmem_time_id range_start;		
		epmem_time_id time_last;

		// update validation count
		my_agent->epmem_validation++;
					
		// create vars table (needed before var queries)
		sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS vars (id INTEGER PRIMARY KEY,value NONE)", -1, &create, &tail );
		sqlite3_step( create );
		sqlite3_finalize( create );

		// left_nodes table (rit)
		sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS left_nodes (min INTEGER, max INTEGER)", -1, &create, &tail );
		sqlite3_step( create );					
		sqlite3_finalize( create );

		// right_nodes table (rit)
		sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS right_nodes (node INTEGER)", -1, &create, &tail );
		sqlite3_step( create );					
		sqlite3_finalize( create );	
		
		// common queries
		sqlite3_prepare_v2( my_agent->epmem_db, "BEGIN", -1, &( my_agent->epmem_statements[ EPMEM_STMT_BEGIN ] ), &tail );
		sqlite3_prepare_v2( my_agent->epmem_db, "COMMIT", -1, &( my_agent->epmem_statements[ EPMEM_STMT_COMMIT ] ), &tail );
		sqlite3_prepare_v2( my_agent->epmem_db, "ROLLBACK", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ROLLBACK ] ), &tail );

		sqlite3_prepare_v2( my_agent->epmem_db, "SELECT value FROM vars WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_VAR_GET ] ), &tail );
		sqlite3_prepare_v2( my_agent->epmem_db, "REPLACE INTO vars (id,value) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_VAR_SET ] ), &tail );

		sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO left_nodes (min,max) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_LEFT ] ), &tail );		
		sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM left_nodes", -1, &( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_LEFT ] ), &tail );
		sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO right_nodes (node) VALUES (?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_RIT_ADD_RIGHT ] ), &tail );
		sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM right_nodes", -1, &( my_agent->epmem_statements[ EPMEM_STMT_RIT_TRUNCATE_RIGHT ] ), &tail );

		// mode - read if existing
		{
			long long stored_mode = NULL;
			if ( epmem_get_variable( my_agent, EPMEM_VAR_MODE, stored_mode ) )
				epmem_set_parameter( my_agent, (const long) EPMEM_PARAM_MODE, (const long) stored_mode );
			else
				epmem_set_variable( my_agent, EPMEM_VAR_MODE, epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG ) );
		}

		// at this point initialize the database for receipt of episodes
		epmem_transaction_begin( my_agent );
		
		// further statement preparation depends upon representation options
		const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );		
		
		if ( ( mode == EPMEM_MODE_ONE ) || ( mode == EPMEM_MODE_TWO ) )
		{
			// variable initialization
			epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, 1 );
			epmem_set_stat( my_agent, (const long) epmem_rit_state_one[ EPMEM_RIT_STATE_OFFSET ], -1 );
			epmem_set_stat( my_agent, (const long) epmem_rit_state_one[ EPMEM_RIT_STATE_LEFTROOT ], 0 );
			epmem_set_stat( my_agent, (const long) epmem_rit_state_one[ EPMEM_RIT_STATE_RIGHTROOT ], 0 );
			epmem_set_stat( my_agent, (const long) epmem_rit_state_one[ EPMEM_RIT_STATE_MINSTEP ], LONG_MAX );
			my_agent->epmem_search_mins->clear();
			my_agent->epmem_search_maxes->clear();
			my_agent->epmem_search_removals->clear();

			// times table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS times (id INTEGER PRIMARY KEY)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// custom statement for inserting times
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO times (id) VALUES (?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ] ), &tail );
			
			////

			// now table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS now (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// now_start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS now_start ON now (start)", -1, &create, &tail );
			sqlite3_step( create );	
			sqlite3_finalize( create );
			
			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS now_id_start ON now (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting now
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO now (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ] ), &tail );

			// custom statement for deleting now
			sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ] ), &tail );

			////

			// point table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS points (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS points_id_start ON points (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );
			
			// start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS points_start ON points (start)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting nodes
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO points (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] ), &tail );

			////

			// episodes table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS episodes (node INTEGER,start INTEGER,end INTEGER,id INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );
			
			// lowerindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS episode_lower ON episodes (node,start)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// upperindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS episode_upper ON episodes (node,end)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS episode_id_start ON episodes (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_end index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS episode_id_end ON episodes (id,end DESC)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// custom statement for inserting episodes
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO episodes (node,start,end,id) VALUES (?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_EPISODE ] ), &tail );			

			////
			
			// ids table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS ids (child_id INTEGER PRIMARY KEY AUTOINCREMENT,parent_id INTEGER,name TEXT,value NONE,hash INTEGER,wme_type INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// hash index for searching
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS id_hash_parent ON ids (hash,parent_id)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting ids
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO ids (parent_id,name,value,hash,wme_type) VALUES (?,?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ] ), &tail );

			// custom statement for finding non-identifier id's
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT child_id FROM ids WHERE hash=? AND parent_id=? AND name=? AND value=? AND wme_type=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] ), &tail );

			// custom statement for finding identifier id's
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT child_id FROM ids WHERE hash=? AND parent_id=? AND name=? AND value IS NULL", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] ), &tail );				

			////

			if ( mode == EPMEM_MODE_TWO )
			{
				// mva table
				sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS mva (time INTEGER, mva_id INTEGER, child_id INTEGER, parent_id INTEGER)", -1, &create, &tail );
				sqlite3_step( create );
				sqlite3_finalize( create );

				// index for retrieval
				sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS mva_time_child ON mva (time, child_id)", -1, &create, &tail );
				sqlite3_step( create );
				sqlite3_finalize( create );

				// custom statement for adding mva info
				sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO mva (time,mva_id,child_id,parent_id) VALUES (?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ] ), &tail );

				// custom statement for getting mva stuff for an episode
				sqlite3_prepare_v2( my_agent->epmem_db, "SELECT m.mva_id, m.child_id, m.parent_id FROM mva m WHERE m.time=? ORDER BY m.child_id ASC", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ] ), &tail );
			}
			
			////

			// custom statement for validating an episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT COUNT(*) AS ct FROM times WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_VALID_EPISODE ] ), &tail );

			// custom statement for finding the next episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id FROM times WHERE id>? ORDER BY id ASC LIMIT 1", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_NEXT_EPISODE ] ), &tail );

			// custom statement for finding the prev episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id FROM times WHERE id<? ORDER BY id DESC LIMIT 1", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_PREV_EPISODE ] ), &tail );

			////

			// custom statement for range intersection query
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT i.child_id, i.parent_id, i.name, i.value, i.wme_type FROM ids i WHERE i.child_id IN (SELECT n.id FROM now n WHERE n.start<= ? UNION ALL SELECT p.id FROM points p WHERE p.start=? UNION ALL SELECT e1.id FROM episodes e1, left_nodes lt WHERE e1.node BETWEEN lt.min AND lt.max AND e1.end >= ? UNION ALL SELECT e2.id FROM episodes e2, right_nodes rt WHERE e2.node = rt.node AND e2.start <= ?) ORDER BY i.child_id ASC", -1, &( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ] ), &tail );

			////

			// get/set RIT variables
			{
				long long var_val;
				
				for ( int i=epmem_rit_state_one[ EPMEM_RIT_STATE_OFFSET ]; i<=epmem_rit_state_one[ EPMEM_RIT_STATE_MINSTEP ]; i++ )
				{
					if ( epmem_get_variable( my_agent, i, var_val ) )
						epmem_set_stat( my_agent, i, var_val );
					else
						epmem_set_variable( my_agent, i, epmem_get_stat( my_agent, i ) );
				}
			}

			// get max time
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT MAX(id) FROM times", -1, &create, &tail );
			if ( sqlite3_step( create ) == SQLITE_ROW )						
				epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, ( sqlite3_column_int64( create, 0 ) + 1 ) );
			sqlite3_finalize( create );
			time_max = epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME );

			// insert non-NOW intervals for all current NOW's
			time_last = ( time_max - 1 );
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id,start FROM now", -1, &create, &tail );				
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 2, time_last );
			while ( sqlite3_step( create ) == SQLITE_ROW )
			{
				range_start = sqlite3_column_int64( create, 1 );

				// point
				if ( range_start == time_last )
				{
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 1, sqlite3_column_int64( create, 0 ) );						
					sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
					sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
				}
				else
					epmem_rit_insert_interval( my_agent, range_start, time_last, sqlite3_column_int64( create, 0 ), epmem_rit_state_one );
			}
			sqlite3_finalize( create );

			// remove all NOW intervals
			sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now", -1, &create, &tail );				
			sqlite3_step( create );
			sqlite3_finalize( create );
			
			// get max id + max list			
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT MAX(child_id) FROM ids", -1, &create, &tail );
			sqlite3_step( create );
			if ( sqlite3_column_type( create, 0 ) != SQLITE_NULL )
			{
				unsigned long long num_ids = sqlite3_column_int64( create, 0 );
				
				my_agent->epmem_search_maxes->resize( num_ids, EPMEM_MEMID_NONE );
				my_agent->epmem_search_mins->resize( num_ids, time_max );
			}
			sqlite3_finalize( create );
		}
		else if ( mode == EPMEM_MODE_THREE )
		{
			// initialize range tracking
			my_agent->epmem_search_mins->clear();
			my_agent->epmem_search_maxes->clear();
			my_agent->epmem_search_removals->clear();

			my_agent->epmem_reconstruct_mins->clear();
			my_agent->epmem_reconstruct_maxes->clear();
			my_agent->epmem_reconstruct_removals->clear();

			// initialize time
			epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, 1 );

			// initialize next_id
			epmem_set_stat( my_agent, (const long) EPMEM_STAT_NEXT_ID, 1 );
			{
				long long stored_id = NULL;
				if ( epmem_get_variable( my_agent, EPMEM_VAR_NEXT_ID, stored_id ) )
					epmem_set_parameter( my_agent, (const long) EPMEM_STAT_NEXT_ID, (const long) stored_id );
				else
					epmem_set_variable( my_agent, EPMEM_VAR_NEXT_ID, epmem_get_stat( my_agent, EPMEM_STAT_NEXT_ID ) );
			}

			// initialize rit state
			for ( int i=EPMEM_RIT_STATE_SEARCH; i<=EPMEM_RIT_STATE_RECONSTRUCT; i++ )
			{
				epmem_set_stat( my_agent, (const long) epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_OFFSET ], -1 );
				epmem_set_stat( my_agent, (const long) epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_LEFTROOT ], 0 );
				epmem_set_stat( my_agent, (const long) epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_RIGHTROOT ], 0 );
				epmem_set_stat( my_agent, (const long) epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_MINSTEP ], LONG_MAX );
			}
			
			////

			// times table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS times (id INTEGER PRIMARY KEY)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// custom statement for inserting times
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO times (id) VALUES (?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_TIME ] ), &tail );

			////

			// now_search table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS now_search (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS now_search_start ON now_search (start)", -1, &create, &tail );
			sqlite3_step( create );	
			sqlite3_finalize( create );
			
			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS now_search_id_start ON now_search (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO now_search (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_SEARCH ] ), &tail );

			// custom statement for deleting
			sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now_search WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_SEARCH ] ), &tail );

			////

			// now_reconstruct table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS now_reconstruct (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS now_reconstruct_start ON now_reconstruct (start)", -1, &create, &tail );
			sqlite3_step( create );	
			sqlite3_finalize( create );
			
			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS now_reconstruct_id_start ON now_reconstruct (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO now_reconstruct (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_RECONSTRUCT ] ), &tail );

			// custom statement for deleting
			sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now_reconstruct WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_RECONSTRUCT ] ), &tail );
			
			////

			// points_search table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS points_search (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS points_search_id_start ON points_search (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );
			
			// start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS points_search_start ON points_search (start)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting nodes
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO points_search (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ] ), &tail );

			////

			// points_reconstruct table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS points_reconstruct (id INTEGER,start INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS points_reconstruct_id_start ON points_search (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );
			
			// start index
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS points_reconstruct_start ON points_reconstruct (start)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting nodes
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO points_reconstruct (id,start) VALUES (?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ] ), &tail );

			////

			// search table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS search (node INTEGER,start INTEGER,end INTEGER,id INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );
			
			// lowerindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS search_lower ON search (node,start)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// upperindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS search_upper ON search (node,end)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_start index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS search_id_start ON search (id,start DESC)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// id_end index (for queries)
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE UNIQUE INDEX IF NOT EXISTS search_id_end ON search (id,end DESC)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for inserting searches
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO search (node,start,end,id) VALUES (?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_SEARCH ] ), &tail );

			////

			// reconstruct table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS reconstruct (node INTEGER,start INTEGER,end INTEGER,id INTEGER)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );
			
			// lowerindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS reconstruct_lower ON reconstruct (node,start)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// upperindex
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS reconstruct_upper ON reconstruct (node,end)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );
			
			// custom statement for inserting reconstructs
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO reconstruct (node,start,end,id) VALUES (?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_RECONSTRUCT ] ), &tail );

			////
			
			// features table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS features (child_id INTEGER PRIMARY KEY AUTOINCREMENT,parent_id INTEGER,name TEXT,value NONE,hash INTEGER,wme_type INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );

			// hash index for feature identification
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS features_parent_hash ON features (parent_id,hash)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for finding features
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT child_id FROM features WHERE parent_id=? AND hash=? AND name=? AND value=? AND wme_type=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ] ), &tail );			
			
			// custom statement for inserting features
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO features (parent_id,name,value,hash,wme_type) VALUES (?,?,?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ] ), &tail );

			////
			
			// paths table
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE TABLE IF NOT EXISTS paths (parent_id INTEGER PRIMARY KEY AUTOINCREMENT,q0 INTEGER,name TEXT,q1 INTEGER)", -1, &create, &tail );
			sqlite3_step( create );					
			sqlite3_finalize( create );			

			// index for path identification
			sqlite3_prepare_v2( my_agent->epmem_db, "CREATE INDEX IF NOT EXISTS paths_q0_name_q1 ON paths (q0,name,q1)", -1, &create, &tail );
			sqlite3_step( create );
			sqlite3_finalize( create );

			// custom statement for finding paths
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT q1 FROM paths WHERE q0=? AND name=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ] ), &tail );			
			
			// custom statement for inserting paths
			sqlite3_prepare_v2( my_agent->epmem_db, "INSERT INTO paths (q0,name,q1) VALUES (?,?,?)", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ] ), &tail );

			////

			// custom statement for validating an episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT COUNT(*) AS ct FROM times WHERE id=?", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_VALID_EPISODE ] ), &tail );

			// custom statement for finding the next episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id FROM times WHERE id>? ORDER BY id ASC LIMIT 1", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_NEXT_EPISODE ] ), &tail );

			// custom statement for finding the prev episode
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id FROM times WHERE id<? ORDER BY id DESC LIMIT 1", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_PREV_EPISODE ] ), &tail );

			////

			// range intersection query: search
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT f.child_id, f.parent_id, f.name, f.value, f.wme_type FROM features f WHERE f.child_id IN (SELECT n.id FROM now_search n WHERE n.start<= ? UNION ALL SELECT p.id FROM points_search p WHERE p.start=? UNION ALL SELECT e1.id FROM search e1, left_nodes lt WHERE e1.node BETWEEN lt.min AND lt.max AND e1.end >= ? UNION ALL SELECT e2.id FROM search e2, right_nodes rt WHERE e2.node = rt.node AND e2.start <= ?) ORDER BY f.child_id ASC", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ] ), &tail );

			// range intersection query: reconstruct
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT f.q0, f.name, f.q1 FROM paths f WHERE f.parent_id IN (SELECT n.id FROM now_reconstruct n WHERE n.start<= ? UNION ALL SELECT p.id FROM points_reconstruct p WHERE p.start=? UNION ALL SELECT e1.id FROM reconstruct e1, left_nodes lt WHERE e1.node BETWEEN lt.min AND lt.max AND e1.end >= ? UNION ALL SELECT e2.id FROM reconstruct e2, right_nodes rt WHERE e2.node = rt.node AND e2.start <= ?) ORDER BY f.q0 ASC, f.q1 ASC", -1, &( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ] ), &tail );

			////

			// get/set RIT variables
			{
				long long var_val;
				int i, j;
				
				for ( i=EPMEM_RIT_STATE_SEARCH; i<=EPMEM_RIT_STATE_RECONSTRUCT; i++ )
				{
					for ( j=epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_OFFSET ]; j<=epmem_rit_state_three[ i ][ EPMEM_RIT_STATE_MINSTEP ]; j++ )
					{
						if ( epmem_get_variable( my_agent, j, var_val ) )
							epmem_set_stat( my_agent, j, var_val );
						else
							epmem_set_variable( my_agent, j, epmem_get_stat( my_agent, j ) );
					}
				}			
			}

			////

			// get max time
			sqlite3_prepare_v2( my_agent->epmem_db, "SELECT MAX(id) FROM times", -1, &create, &tail );
			if ( sqlite3_step( create ) == SQLITE_ROW )						
				epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, ( sqlite3_column_int64( create, 0 ) + 1 ) );
			sqlite3_finalize( create );
			time_max = epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME );

			// insert non-NOW intervals for all current NOW's
			// remove NOW's
			{
				time_last = ( time_max - 1 );
				
				// search
				{
					sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id,start FROM now_search", -1, &create, &tail );				
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ], 2, time_last );
					while ( sqlite3_step( create ) == SQLITE_ROW )
					{
						range_start = sqlite3_column_int64( create, 1 );

						// point
						if ( range_start == time_last )
						{
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ], 1, sqlite3_column_int64( create, 0 ) );						
							sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ] );
							sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ] );
						}
						else
							epmem_rit_insert_interval( my_agent, range_start, time_last, sqlite3_column_int64( create, 0 ), epmem_rit_state_three[ EPMEM_RIT_STATE_SEARCH ] );
					}
					sqlite3_finalize( create );
					
					sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now_search", -1, &create, &tail );				
					sqlite3_step( create );
					sqlite3_finalize( create );
				}

				// reconstruct
				{					
					sqlite3_prepare_v2( my_agent->epmem_db, "SELECT id,start FROM now_reconstruct", -1, &create, &tail );				
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ], 2, time_last );
					while ( sqlite3_step( create ) == SQLITE_ROW )
					{
						range_start = sqlite3_column_int64( create, 1 );

						// point
						if ( range_start == time_last )
						{
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ], 1, sqlite3_column_int64( create, 0 ) );						
							sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ] );
							sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ] );
						}
						else
							epmem_rit_insert_interval( my_agent, range_start, time_last, sqlite3_column_int64( create, 0 ), epmem_rit_state_three[ EPMEM_RIT_STATE_RECONSTRUCT ] );
					}
					sqlite3_finalize( create );

					sqlite3_prepare_v2( my_agent->epmem_db, "DELETE FROM now_reconstruct", -1, &create, &tail );				
					sqlite3_step( create );
					sqlite3_finalize( create );
				}
			}

			// get max id + max list
			{
				// features
				{
					sqlite3_prepare_v2( my_agent->epmem_db, "SELECT MAX(child_id) FROM features", -1, &create, &tail );
					sqlite3_step( create );
					if ( sqlite3_column_type( create, 0 ) != SQLITE_NULL )
					{
						unsigned long long num_ids = sqlite3_column_int64( create, 0 );
						
						my_agent->epmem_search_maxes->resize( num_ids, EPMEM_MEMID_NONE );
						my_agent->epmem_search_mins->resize( num_ids, time_max );
					}
					sqlite3_finalize( create );
				}


				// paths
				{
					sqlite3_prepare_v2( my_agent->epmem_db, "SELECT MAX(parent_id) FROM paths", -1, &create, &tail );
					sqlite3_step( create );
					if ( sqlite3_column_type( create, 0 ) != SQLITE_NULL )
					{
						unsigned long long num_ids = sqlite3_column_int64( create, 0 );
						
						my_agent->epmem_reconstruct_maxes->resize( num_ids, EPMEM_MEMID_NONE );
						my_agent->epmem_reconstruct_mins->resize( num_ids, time_max );
					}
					sqlite3_finalize( create );
				}
			}
		}

		epmem_transaction_end( my_agent, true );
		epmem_transaction_begin( my_agent );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_INIT );
	////////////////////////////////////////////////////////////////////////////
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Storage Functions (epmem::storage)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// adds a new episode to the store
void epmem_new_episode( agent *my_agent )
{	
	// if this is the first episode, initialize db components	
	if ( my_agent->epmem_db_status == -1 )
		epmem_init_db( my_agent );
	
	// add the episode only if db is properly initialized
	if ( my_agent->epmem_db_status != SQLITE_OK )
		return;

	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_STORAGE );
	////////////////////////////////////////////////////////////////////////////

	epmem_time_id time_counter = epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME );

	// provide trace output
	if ( my_agent->sysparams[ TRACE_EPMEM_SYSPARAM ] )
	{
		char buf[256];
		SNPRINTF( buf, 254, "NEW EPISODE: (%c%d, %d)", my_agent->bottom_goal->id.name_letter, my_agent->bottom_goal->id.name_number, time_counter );
		
		print( my_agent, buf );
		
		xml_generate_warning( my_agent, buf );
	}	
	
	const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );
	
	if ( mode == EPMEM_MODE_ONE )
	{
		// for now we are only recording episodes at the top state
		Symbol *parent_sym;

		wme **wmes = NULL;
		int len = 0;
		
		std::queue<Symbol *> syms;
		std::queue<epmem_node_id> ids;		

		epmem_node_id parent_id;
		std::map<epmem_node_id, bool> epmem;

		unsigned long my_hash;
		int tc = get_new_tc_number( my_agent );

		int i;

		// prevent recording exclusions
		std::list<const char *>::iterator exclusion;
		bool should_exclude;

		syms.push( my_agent->top_goal );
		ids.push( EPMEM_MEMID_ROOT );
		
		while ( !syms.empty() )
		{		
			parent_sym = syms.front();
			syms.pop();

			parent_id = ids.front();
			ids.pop();

			wmes = epmem_get_augs_of_id( my_agent, parent_sym, tc, &len );

			if ( wmes != NULL )
			{
				for ( i=0; i<len; i++ )
				{
					// prevent exclusions from being recorded
					should_exclude = false;
					for ( exclusion=my_agent->epmem_exclusions->begin(); 
						  ( ( !should_exclude ) && ( exclusion!=my_agent->epmem_exclusions->end() ) ); 
						  exclusion++ )
						if ( strcmp( (const char *) wmes[i]->attr->sc.name, (*exclusion) ) == 0 )
							should_exclude = true;
					if ( should_exclude )
						continue;
					
					if ( ( wmes[i]->epmem_id == NULL ) || ( wmes[i]->epmem_valid != my_agent->epmem_validation ) )
					{					
						wmes[i]->epmem_id = NULL;
						wmes[i]->epmem_valid = my_agent->epmem_validation;

						my_hash = epmem_hash_wme( wmes[i] );
						if ( wmes[i]->value->common.symbol_type != IDENTIFIER_SYMBOL_TYPE )
						{					
							// hash=? AND parent_id=? AND name=? AND value=? AND wme_type=?
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 1, my_hash );
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 2, parent_id );
							sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 3, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );
							switch( wmes[i]->value->common.symbol_type )
							{
								case SYM_CONSTANT_SYMBOL_TYPE:
									sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
									break;
						            
								case INT_CONSTANT_SYMBOL_TYPE:
		        					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, wmes[i]->value->ic.value );
									break;
					
								case FLOAT_CONSTANT_SYMBOL_TYPE:
		        					sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, wmes[i]->value->fc.value );
									break;
							}
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 5, wmes[i]->value->common.symbol_type );
							
							if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] ) == SQLITE_ROW )
								wmes[i]->epmem_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 0 );
							
							sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] );
						}
						else
						{
							// hash=? AND parent_id=? AND name=? AND value IS NULL							
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 1, my_hash );
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 2, parent_id );
							sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 3, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );

							if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] ) == SQLITE_ROW )
								wmes[i]->epmem_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 0 );
							
							sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] );
						}
					}					
										
					// insert on no id
					if ( wmes[i]->epmem_id == NULL )
					{						
						long long wme_type = wmes[i]->value->common.symbol_type;
						
						// insert (parent_id,name,value,hash)						
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 1, parent_id );
						sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 2, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );				
						switch ( wme_type )
						{
							case SYM_CONSTANT_SYMBOL_TYPE:
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
								break;
								
							case INT_CONSTANT_SYMBOL_TYPE:
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, wmes[i]->value->ic.value );
								break;
								
							case FLOAT_CONSTANT_SYMBOL_TYPE:
								sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, wmes[i]->value->fc.value );
								break;
								
							case IDENTIFIER_SYMBOL_TYPE:
								sqlite3_bind_null( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3 );
								break;
						}
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 4, my_hash );
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 5, wme_type );
						sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ] );
						sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ] );					

						wmes[i]->epmem_id = sqlite3_last_insert_rowid( my_agent->epmem_db );

						// new nodes definitely start
						epmem[ wmes[i]->epmem_id ] = true;
						my_agent->epmem_search_mins->push_back( time_counter );
						my_agent->epmem_search_maxes->push_back( time_counter );
					}
					else
					{
						// definitely don't update/delete
						(*my_agent->epmem_search_removals)[ wmes[i]->epmem_id ] = false;

						// we insert if current time is > 1+ max
						if ( (*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] < ( time_counter - 1 ) )
							epmem[ wmes[i]->epmem_id ] = true;

						// update max irrespectively
						(*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] = time_counter;
					}
					
					// keep track of identifiers (for further study)
					if ( wmes[i]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
					{
						syms.push( wmes[i]->value );
						ids.push( wmes[i]->epmem_id );
					}				
				}

				// free space from aug list
				free_memory( my_agent, wmes, MISCELLANEOUS_MEM_USAGE );
			}
		}
				
		// all inserts at once (provides unique)
		std::map<epmem_node_id, bool>::iterator e = epmem.begin();
		while ( e != epmem.end() )
		{
			// add NOW entry
			// id = ?, start = ?			
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ], 1, e->first );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ], 2, time_counter );
			sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ] );
			sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ] );

			// update min
			(*my_agent->epmem_search_mins)[ e->first - 1 ] = time_counter;

			e++;
		}		

		// all removals at once
		std::map<epmem_node_id, bool>::iterator r = my_agent->epmem_search_removals->begin();
		epmem_time_id range_start;
		epmem_time_id range_end;
		while ( r != my_agent->epmem_search_removals->end() )
		{
			if ( r->second )
			{			
				// remove NOW entry
				// id = ?
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ], 1, r->first );				
				sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ] );
				sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ] );

				range_start = (*my_agent->epmem_search_mins)[ r->first - 1 ];
				range_end = ( time_counter - 1 );

				// point (id, start)
				if ( range_start == range_end )
				{
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 1, r->first );
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 2, range_start );					
					sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
					sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
				}
				// node
				else				
					epmem_rit_insert_interval( my_agent, range_start, range_end, r->first, epmem_rit_state_one );
			}
			
			r++;
		}
		my_agent->epmem_search_removals->clear();

		// add the time id to the times table
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ], 1, time_counter );
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ] );
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ] );

		epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, time_counter + 1 );
	}
	else if ( mode == EPMEM_MODE_TWO )
	{
		wme **wmes = NULL;
		int len = 0;
		
		std::queue<Symbol *> syms;
		std::queue<epmem_node_id> ids;
		Symbol *parent_sym;
		epmem_node_id parent_id;

		std::queue<epmem_node_id> mva_parents;
		std::queue< std::map<char *, bool> * > mva_gcs;
		epmem_node_id mva_parent;
		std::map<char *, bool> *mva_gc;
		
		std::map<epmem_node_id, bool> epmem;

		unsigned long my_hash;
		int tc = get_new_tc_number( my_agent );

		int i, j;

		// mva vars
		epmem_node_id mva_counter = 1;
		epmem_node_id mva_id;
		bool mva_gc_check = false;
		std::map<char *, int> mva_attrib_count;
		int *mva_attrib_counter;
		std::map<char *, std::map<char *, bool> > mva_gc_bad;
		std::map<char *, std::map<char *, int> > mva_gc_counts;
		std::map<char *, std::map<char *, int> >::iterator mva_gc_attrib_counter;
		std::map<char *, int>::iterator mva_gc_counter;			
		wme **mva_gc_wmes = NULL;
		int mva_gc_len = 0;

		std::map<Symbol *, wme**> wmes_cache;
		std::map<Symbol *, wme**>::iterator wmes_cache_p;
		std::map<Symbol *, int> wmes_len_cache;
		std::map<Symbol *, int>::iterator wmes_len_cache_p;

		// prevent recording exclusions
		std::list<const char *>::iterator exclusion;
		bool *exclusion_quickies;

		// initialize lists
		syms.push( my_agent->top_goal );
		ids.push( EPMEM_MEMID_ROOT );

		mva_parents.push( EPMEM_MEMID_NONE );
		mva_gcs.push( NULL );
		
		while ( !syms.empty() )
		{		
			parent_sym = syms.front();
			syms.pop();

			parent_id = ids.front();
			ids.pop();

			mva_parent = mva_parents.front();
			mva_parents.pop();

			mva_gc = mva_gcs.front();
			mva_gcs.pop();

			mva_attrib_count.clear();
			mva_gc_check = false;
			mva_gc_bad.clear();
			mva_gc_counts.clear();

			// attempt to use cache to avoid re-walking wm many times over				
			wmes_cache_p = wmes_cache.find( parent_sym );
			if ( wmes_cache_p != wmes_cache.end() )
			{
				wmes_len_cache_p = wmes_len_cache.find( parent_sym );
				len = wmes_len_cache_p->second;
				wmes_len_cache.erase( wmes_len_cache_p );
				
				wmes = wmes_cache_p->second;
				wmes_cache.erase( wmes_cache_p );
			}
			else
			{
				wmes = epmem_get_augs_of_id( my_agent, parent_sym, tc, &len );
			}

			if ( wmes != NULL )
			{
				// allocate for quick exclusion access
				exclusion_quickies = new bool[ len ];
				
				// get attribute counts, get quicky exclusions at the same time
				for ( i=0; i<len; i++ )
				{						
					exclusion_quickies[ i ] = wmes[i]->acceptable;

					for ( exclusion=my_agent->epmem_exclusions->begin(); 
						  ( ( !exclusion_quickies[ i ] ) && ( exclusion!=my_agent->epmem_exclusions->end() ) ); 
						  exclusion++ )
						if ( strcmp( (const char *) wmes[i]->attr->sc.name, (*exclusion) ) == 0 )
							exclusion_quickies[ i ] = true;
					if ( exclusion_quickies[ i ] )						
						continue;

					if ( wmes[i]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
					{
						mva_attrib_counter = &mva_attrib_count[ wmes[i]->attr->sc.name ];
						(*mva_attrib_counter)++;

						if ( (*mva_attrib_counter) > 1 )
							mva_gc_check = true;
					}
				}
				mva_attrib_counter = NULL;

				// if we have at least one mva, have to check grandchildren (gc)
				if ( mva_gc_check )
				{
					for ( i=0; i<len; i++ )
					{
						if ( !exclusion_quickies[ i ] )
						{
							// if an mva, examine gc's
							if ( ( wmes[i]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE ) &&
								 ( mva_attrib_count[ wmes[i]->attr->sc.name ] > 1 ) )
							{									
								mva_gc_wmes = epmem_get_augs_of_id( my_agent, wmes[i]->value, tc, &mva_gc_len );

								if ( mva_gc_wmes != NULL )
								{
									wmes_cache[ wmes[i]->value ] = mva_gc_wmes;
									wmes_len_cache[ wmes[i]->value ] = mva_gc_len;

									for ( j=0; j<mva_gc_len; j++ )
										if ( mva_gc_wmes[j]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
											mva_gc_counts[ wmes[i]->attr->sc.name ][ mva_gc_wmes[j]->attr->sc.name ]++;
								}
							}
						}
					}

					mva_gc_attrib_counter = mva_gc_counts.begin();
					while ( mva_gc_attrib_counter != mva_gc_counts.end() )
					{
						mva_gc_counter = mva_gc_attrib_counter->second.begin();
						while ( mva_gc_counter != mva_gc_attrib_counter->second.end() )
						{
							if ( mva_gc_counter->second > 1 )									
								mva_gc_bad[ mva_gc_attrib_counter->first ][ mva_gc_counter->first ] = true;

							mva_gc_counter++;
						}
						
						mva_gc_attrib_counter++;
					}
				}

				for ( i=0; i<len; i++ )
				{
					if ( !exclusion_quickies[ i ] )
					{
						// get id if exists
						if ( ( wmes[i]->epmem_id == NULL ) || ( wmes[i]->epmem_valid != my_agent->epmem_validation ) )
						{					
							wmes[i]->epmem_id = NULL;
							wmes[i]->epmem_valid = my_agent->epmem_validation;

							my_hash = epmem_hash_wme( wmes[i] );
							if ( wmes[i]->value->common.symbol_type != IDENTIFIER_SYMBOL_TYPE )
							{					
								// hash=? AND parent_id=? AND name=? AND value=? AND wme_type=?
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 1, my_hash );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 2, parent_id );
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 3, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );
								switch( wmes[i]->value->common.symbol_type )
								{
									case SYM_CONSTANT_SYMBOL_TYPE:
										sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
										break;
							            
									case INT_CONSTANT_SYMBOL_TYPE:
		        						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, wmes[i]->value->ic.value );
										break;
						
									case FLOAT_CONSTANT_SYMBOL_TYPE:
		        						sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, wmes[i]->value->fc.value );
										break;
								}
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 5, wmes[i]->value->common.symbol_type );
								
								if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] ) == SQLITE_ROW )
									wmes[i]->epmem_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 0 );
								
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] );
							}
							else
							{
								// hash=? AND parent_id=? AND name=? AND value IS NULL							
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 1, my_hash );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 2, parent_id );
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 3, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );

								if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] ) == SQLITE_ROW )
									wmes[i]->epmem_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 0 );
								
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] );
							}
						}

						// insert on no id
						if ( wmes[i]->epmem_id == NULL )
						{						
							long long wme_type = wmes[i]->value->common.symbol_type;
							
							// insert (parent_id,name,value,hash)						
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 1, parent_id );
							sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 2, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );				
							switch ( wme_type )
							{
								case SYM_CONSTANT_SYMBOL_TYPE:
									sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
									break;
									
								case INT_CONSTANT_SYMBOL_TYPE:
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, wmes[i]->value->ic.value );
									break;
									
								case FLOAT_CONSTANT_SYMBOL_TYPE:
									sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3, wmes[i]->value->fc.value );
									break;
									
								case IDENTIFIER_SYMBOL_TYPE:
									sqlite3_bind_null( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 3 );
									break;
							}
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 4, my_hash );
							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ], 5, wme_type );
							sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ] );
							sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_ID ] );					

							wmes[i]->epmem_id = sqlite3_last_insert_rowid( my_agent->epmem_db );

							// new nodes definitely start
							epmem[ wmes[i]->epmem_id ] = true;
							my_agent->epmem_search_mins->push_back( time_counter );
							my_agent->epmem_search_maxes->push_back( time_counter );
						}
						else
						{
							// definitely don't update/delete
							(*my_agent->epmem_search_removals)[ wmes[i]->epmem_id ] = false;

							// we insert if current time is > 1+ max
							if ( (*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] < ( time_counter - 1 ) )
								epmem[ wmes[i]->epmem_id ] = true;

							// update max irrespectively
							(*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] = time_counter;
						}

						// keep track of identifiers (for further study)
						if ( wmes[i]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
						{
							syms.push( wmes[i]->value );
							ids.push( wmes[i]->epmem_id );

							if ( ( mva_parent != EPMEM_MEMID_NONE ) || ( mva_attrib_count[ wmes[i]->attr->sc.name ] > 1 ) )
							{
								mva_id = EPMEM_MEMID_NONE;
								if ( ( mva_attrib_count[ wmes[i]->attr->sc.name ] > 1 ) || ( mva_gc->find( wmes[i]->attr->sc.name ) != mva_gc->end() ) )
								{
									mva_id = mva_counter++;
									mva_gcs.push( new std::map<char *, bool>( mva_gc_bad[ wmes[i]->attr->sc.name ] ) );
								}
								else
									mva_gcs.push( NULL );

								mva_parents.push( mva_id );

								// (time,mva_id,child_id,parent_id)
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 1, time_counter );

								if ( mva_id == EPMEM_MEMID_NONE )
									sqlite3_bind_null( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 2 );
								else
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 2, mva_id );

								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 3, wmes[i]->epmem_id );

								if ( mva_parent == EPMEM_MEMID_NONE )
									sqlite3_bind_null( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 4 );
								else
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 4, mva_parent );

								sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ] );
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ] );
							}
							else
							{
								mva_parents.push( EPMEM_MEMID_NONE );
								mva_gcs.push( NULL );
							}
						}
						// might need non-identifier mva
						else
						{
							if ( mva_parent != EPMEM_MEMID_NONE )
							{
								// (time,mva_id,child_id,parent_id)
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 1, time_counter );
								sqlite3_bind_null( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 2 );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 3, wmes[i]->epmem_id );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ], 4, mva_parent );
								sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ] );
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_ADD_ID ] );
							}
						}
					}
				}

				// free space from aug list
				free_memory( my_agent, wmes, MISCELLANEOUS_MEM_USAGE );

				// free space from quicky exclusion list
				delete [] exclusion_quickies;
			}

			if ( mva_gc != NULL )
				delete mva_gc;
		}
				
		// all inserts at once (provides unique)
		std::map<epmem_node_id, bool>::iterator e = epmem.begin();
		while ( e != epmem.end() )
		{
			// add NOW entry
			// id = ?, start = ?			
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ], 1, e->first );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ], 2, time_counter );
			sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ] );
			sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_NOW ] );

			// update min
			(*my_agent->epmem_search_mins)[ e->first - 1 ] = time_counter;

			e++;
		}		

		// all removals at once
		std::map<epmem_node_id, bool>::iterator r = my_agent->epmem_search_removals->begin();
		epmem_time_id range_start;
		epmem_time_id range_end;
		while ( r != my_agent->epmem_search_removals->end() )
		{
			if ( r->second )
			{			
				// remove NOW entry
				// id = ?
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ], 1, r->first );				
				sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ] );
				sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_DELETE_NOW ] );

				range_start = (*my_agent->epmem_search_mins)[ r->first - 1 ];
				range_end = ( time_counter - 1 );

				// point (id, start)
				if ( range_start == range_end )
				{
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 1, r->first );
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ], 2, range_start );					
					sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
					sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_POINT ] );
				}
				// node
				else				
					epmem_rit_insert_interval( my_agent, range_start, range_end, r->first, epmem_rit_state_one );
			}
			
			r++;
		}
		my_agent->epmem_search_removals->clear();

		// add the time id to the times table
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ], 1, time_counter );
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ] );
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_ADD_TIME ] );

		epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, time_counter + 1 );
	}
	else if ( mode == EPMEM_MODE_THREE )
	{
		std::map<epmem_node_id, bool> seen_ids;
		std::map<epmem_node_id, bool>::iterator seen_p;
		
		std::queue<Symbol *> parent_syms;
		Symbol *parent_sym;
		std::queue<epmem_node_id> parent_ids;
		epmem_node_id parent_id;

		std::queue<epmem_node_id> epmem_search;
		std::queue<epmem_node_id> epmem_reconstruct;

		unsigned long my_hash;
		int tc = get_new_tc_number( my_agent );

		wme **wmes = NULL;
		int len = 0;
		int i;

		// prevent recording exclusions
		std::list<const char *>::iterator exclusion;
		bool should_exclude;

		parent_syms.push( my_agent->top_goal );
		parent_ids.push( EPMEM_MEMID_ROOT );
		
		while ( !parent_syms.empty() )
		{
			parent_sym = parent_syms.front();
			parent_syms.pop();

			parent_id = parent_ids.front();
			parent_ids.pop();

			wmes = epmem_get_augs_of_id( my_agent, parent_sym, tc, &len );

			if ( wmes != NULL )
			{
				for ( i=0; i<len; i++ )
				{
					// prevent exclusions from being recorded
					should_exclude = false;
					for ( exclusion=my_agent->epmem_exclusions->begin(); 
						  ( ( !should_exclude ) && ( exclusion!=my_agent->epmem_exclusions->end() ) ); 
						  exclusion++ )
						if ( strcmp( (const char *) wmes[i]->attr->sc.name, (*exclusion) ) == 0 )
							should_exclude = true;
					if ( should_exclude )
						continue;

					if ( wmes[i]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
					{					
						// path id in cache?
						if ( ( wmes[i]->epmem_id == NULL ) || ( wmes[i]->epmem_valid != my_agent->epmem_validation ) )
						{							
							wmes[i]->epmem_valid = my_agent->epmem_validation;
														
							// add path
							{								
								if ( wmes[i]->value->id.epmem_id == NULL )
								{
									// update next id
									wmes[i]->value->id.epmem_id = epmem_get_stat( my_agent, EPMEM_STAT_NEXT_ID );
									epmem_set_stat( my_agent, EPMEM_STAT_NEXT_ID, wmes[i]->value->id.epmem_id + 1 );
									epmem_set_variable( my_agent, EPMEM_VAR_NEXT_ID, wmes[i]->value->id.epmem_id + 1);
								}
								
								// insert (q0,w,q1)
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ], 1, parent_id );
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ], 2, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );	
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ], 3, wmes[i]->value->id.epmem_id );								
								sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ] );
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_PATH ] );					

								wmes[i]->epmem_id = sqlite3_last_insert_rowid( my_agent->epmem_db );

								// new nodes definitely start
								epmem_reconstruct.push( wmes[i]->epmem_id );
								my_agent->epmem_reconstruct_mins->push_back( time_counter );
								my_agent->epmem_reconstruct_maxes->push_back( time_counter );
							}						
						}
						
						// check if seen before
						seen_p = seen_ids.find( wmes[i]->value->id.epmem_id );
						if ( seen_p == seen_ids.end() )
						{
							// future exploration
							parent_syms.push( wmes[i]->value );
							parent_ids.push( wmes[i]->value->id.epmem_id );
						}
					}
					else
					{
						// feature id in cache?
						if ( ( wmes[i]->epmem_id == NULL ) || ( wmes[i]->epmem_valid != my_agent->epmem_validation ) )
						{
							wmes[i]->epmem_id = NULL;							
							wmes[i]->epmem_valid = my_agent->epmem_validation;

							my_hash = epmem_hash_wme( wmes[i] );
							
							// try to get feature id
							{
								// parent_id=? AND hash=? AND name=? AND value=? AND wme_type=?								
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 1, parent_id );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 2, my_hash );
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 3, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );
								switch( wmes[i]->value->common.symbol_type )
								{
									case SYM_CONSTANT_SYMBOL_TYPE:
										sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
										break;
							            
									case INT_CONSTANT_SYMBOL_TYPE:
		        						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, wmes[i]->value->ic.value );
										break;
						
									case FLOAT_CONSTANT_SYMBOL_TYPE:
		        						sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, wmes[i]->value->fc.value );
										break;
								}
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 5, wmes[i]->value->common.symbol_type );
								
								if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ] ) == SQLITE_ROW )
									wmes[i]->epmem_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 0 );
								
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ] );
							}

							// act depending on new/existing feature
							if ( wmes[i]->epmem_id == NULL )
							{
								long long wme_type = wmes[i]->value->common.symbol_type;
						
								// insert (parent_id,name,value,hash,type)
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 1, parent_id );
								sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 2, (const char *) wmes[i]->attr->sc.name, -1, SQLITE_STATIC );				
								switch ( wme_type )
								{
									case SYM_CONSTANT_SYMBOL_TYPE:
										sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 3, (const char *) wmes[i]->value->sc.name, -1, SQLITE_STATIC );
										break;
										
									case INT_CONSTANT_SYMBOL_TYPE:
										sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 3, wmes[i]->value->ic.value );
										break;
										
									case FLOAT_CONSTANT_SYMBOL_TYPE:
										sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 3, wmes[i]->value->fc.value );
										break;									
								}
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 4, my_hash );
								sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ], 5, wme_type );
								sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ] );
								sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_FEATURE ] );					

								wmes[i]->epmem_id = sqlite3_last_insert_rowid( my_agent->epmem_db );

								// new nodes definitely start
								epmem_search.push( wmes[i]->epmem_id );
								my_agent->epmem_search_mins->push_back( time_counter );
								my_agent->epmem_search_maxes->push_back( time_counter );
							}
							else
							{
								// definitely don't update/delete
								(*my_agent->epmem_search_removals)[ wmes[i]->epmem_id ] = false;

								// we insert if current time is > 1+ max
								if ( (*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] < ( time_counter - 1 ) )
									epmem_search.push( wmes[i]->epmem_id );

								// update max irrespectively
								(*my_agent->epmem_search_maxes)[ wmes[i]->epmem_id - 1 ] = time_counter;
							}
						}						
					}
				}

				// free space from aug list
				free_memory( my_agent, wmes, MISCELLANEOUS_MEM_USAGE );
			}
		}

		// all inserts
		{
			epmem_node_id *temp_node;
			
			// search
			while ( !epmem_search.empty() )
			{			
				temp_node =& epmem_search.front();
				
				// add NOW entry
				// id = ?, start = ?			
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_SEARCH ], 1, (*temp_node) );
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_SEARCH ], 2, time_counter );
				sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_SEARCH ] );
				sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_SEARCH ] );

				// update min
				(*my_agent->epmem_search_mins)[ (*temp_node) - 1 ] = time_counter;

				epmem_search.pop();
			}

			// reconstruct
			while ( !epmem_reconstruct.empty() )
			{			
				temp_node =& epmem_reconstruct.front();
				
				// add NOW entry
				// id = ?, start = ?			
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_RECONSTRUCT ], 1, (*temp_node) );
				sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_RECONSTRUCT ], 2, time_counter );
				sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_RECONSTRUCT ] );
				sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_NOW_RECONSTRUCT ] );

				// update min
				(*my_agent->epmem_reconstruct_mins)[ (*temp_node) - 1 ] = time_counter;

				epmem_reconstruct.pop();
			}
		}

		// all removals
		{			
			std::map<epmem_node_id, bool>::iterator r;
			epmem_time_id range_start;
			epmem_time_id range_end;
			
			// search
			r = my_agent->epmem_search_removals->begin();			
			while ( r != my_agent->epmem_search_removals->end() )
			{
				if ( r->second )
				{			
					// remove NOW entry
					// id = ?
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_SEARCH ], 1, r->first );				
					sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_SEARCH ] );
					sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_SEARCH ] );

					range_start = (*my_agent->epmem_search_mins)[ r->first - 1 ];
					range_end = ( time_counter - 1 );

					// point (id, start)
					if ( range_start == range_end )
					{
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ], 1, r->first );
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ], 2, range_start );					
						sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ] );
						sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_SEARCH ] );
					}
					// node
					else				
						epmem_rit_insert_interval( my_agent, range_start, range_end, r->first, epmem_rit_state_three[ EPMEM_RIT_STATE_SEARCH ] );
				}
				
				r++;
			}
			my_agent->epmem_search_removals->clear();

			// reconstruct
			r = my_agent->epmem_reconstruct_removals->begin();			
			while ( r != my_agent->epmem_reconstruct_removals->end() )
			{
				if ( r->second )
				{			
					// remove NOW entry
					// id = ?
					sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_RECONSTRUCT ], 1, r->first );				
					sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_RECONSTRUCT ] );
					sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_DELETE_NOW_RECONSTRUCT ] );

					range_start = (*my_agent->epmem_reconstruct_mins)[ r->first - 1 ];
					range_end = ( time_counter - 1 );

					// point (id, start)
					if ( range_start == range_end )
					{
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ], 1, r->first );
						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ], 2, range_start );					
						sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ] );
						sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_POINT_RECONSTRUCT ] );
					}
					// node
					else				
						epmem_rit_insert_interval( my_agent, range_start, range_end, r->first, epmem_rit_state_three[ EPMEM_RIT_STATE_RECONSTRUCT ] );
				}
				
				r++;
			}
			my_agent->epmem_reconstruct_removals->clear();
		}

		// add the time id to the times table
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_TIME ], 1, time_counter );
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_TIME ] );
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_ADD_TIME ] );

		epmem_set_stat( my_agent, (const long) EPMEM_STAT_TIME, time_counter + 1 );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_STORAGE );
	////////////////////////////////////////////////////////////////////////////
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Non-Cue-Based Retrieval Functions (epmem::ncb)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// returns true if the temporal id is valid
bool epmem_valid_episode( agent *my_agent, epmem_time_id memory_id )
{
	const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );
	bool return_val = false;	
	
	if ( ( mode == EPMEM_MODE_ONE ) || ( mode == EPMEM_MODE_TWO ) ) 
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_VALID_EPISODE ], 1, memory_id );
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_VALID_EPISODE ] );
		return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_VALID_EPISODE ], 0 ) > 0 );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_VALID_EPISODE ] );
	}
	else if ( mode == EPMEM_MODE_THREE )
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_VALID_EPISODE ], 1, memory_id );
		sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_VALID_EPISODE ] );
		return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_VALID_EPISODE ], 0 ) > 0 );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_VALID_EPISODE ] );
	}

	return return_val;
}

// reconstructs an episode in working memory
void epmem_install_memory( agent *my_agent, Symbol *state, epmem_time_id memory_id )
{
	wme *new_wme;

	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_NCB_RETRIEVAL );
	////////////////////////////////////////////////////////////////////////////
	
	// get the ^result header for this state
	Symbol *result_header = state->id.epmem_result_header;

	// if no memory, say so
	if ( ( memory_id == EPMEM_MEMID_NONE ) ||
		 !epmem_valid_episode( my_agent, memory_id ) )
	{	
		new_wme = add_input_wme( my_agent, result_header, my_agent->epmem_retrieved_symbol, my_agent->epmem_no_memory_symbol );
		new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
		state->id.epmem_info->epmem_wmes->push( new_wme );

		state->id.epmem_info->last_memory = EPMEM_MEMID_NONE;
		
		epmem_stop_timer( my_agent, EPMEM_TIMER_NCB_RETRIEVAL );
		return;
	}

	// remember this as the last memory installed
	state->id.epmem_info->last_memory = memory_id;	

	// create a new ^retrieved header for this result
	Symbol *retrieved_header = make_new_identifier( my_agent, 'R', result_header->id.level );
	new_wme = add_input_wme( my_agent, result_header, my_agent->epmem_retrieved_symbol, retrieved_header );
	new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
	state->id.epmem_info->epmem_wmes->push( new_wme );
	symbol_remove_ref( my_agent, retrieved_header );

	// add *-id wme's
	new_wme = add_input_wme( my_agent, result_header, my_agent->epmem_memory_id_symbol, make_int_constant( my_agent, memory_id ) );
	new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
	state->id.epmem_info->epmem_wmes->push( new_wme );
	new_wme = add_input_wme( my_agent, result_header, my_agent->epmem_present_id_symbol, make_int_constant( my_agent, epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME ) ) );
	new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
	state->id.epmem_info->epmem_wmes->push( new_wme );

	const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );

	if ( mode == EPMEM_MODE_ONE )
	{		
		std::map<epmem_node_id, Symbol *> ids;
		epmem_node_id child_id;
		epmem_node_id parent_id;
		const char *name;
		long long wme_type;
		Symbol *attr = NULL;
		Symbol *value = NULL;
		Symbol *parent = NULL;

		ids[ 0 ] = retrieved_header;

		epmem_rit_prep_left_right( my_agent, memory_id, memory_id, epmem_rit_state_one );

		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 1, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 2, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 4, memory_id );
		while ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ] ) == SQLITE_ROW )
		{			
			// e.id, i.parent_id, i.name, i.value
			child_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 0 );
			parent_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 1 );
			name = (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 2 );
			wme_type = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 4 );
			
			// make a symbol to represent the attribute name		
			attr = make_sym_constant( my_agent, const_cast<char *>( name ) );

			// get a reference to the parent
			parent = ids[ parent_id ];

			// identifier = NULL, else attr->val
			if ( wme_type == IDENTIFIER_SYMBOL_TYPE )
			{
				value = make_new_identifier( my_agent, name[0], parent->id.level );				
				
				new_wme = add_input_wme( my_agent, parent, attr, value );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
				state->id.epmem_info->epmem_wmes->push( new_wme );

				symbol_remove_ref( my_agent, value );

				ids[ child_id ] = value;
			}
			else
			{
				switch ( wme_type )
				{
					case INT_CONSTANT_SYMBOL_TYPE:
						value = make_int_constant( my_agent, sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
						break;

					case FLOAT_CONSTANT_SYMBOL_TYPE:
						value = make_float_constant( my_agent, sqlite3_column_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
						break;

					case SYM_CONSTANT_SYMBOL_TYPE:						
						value = make_sym_constant( my_agent, const_cast<char *>( (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) ) );
						break;
				}

				new_wme = add_input_wme( my_agent, parent, attr, value );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
				state->id.epmem_info->epmem_wmes->push( new_wme );
			}
		}
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ] );

		epmem_rit_clear_left_right( my_agent );
	}
	else if ( mode == EPMEM_MODE_TWO )
	{
		std::map<epmem_node_id, Symbol *> ids;
		std::map<epmem_node_id, Symbol *> mva_ids;

		epmem_node_id child_id;
		epmem_node_id parent_id;
		epmem_node_id mva_child_id;

		const char *name;
		long long wme_type;
		Symbol *attr = NULL;
		Symbol *value = NULL;

		std::map<epmem_node_id, Symbol *>::iterator parent;

		ids[ 0 ] = retrieved_header;

		// prep mva
		// m.mva_id, m.child_id, m.parent_id
		mva_child_id = EPMEM_MEMID_NONE;
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 1, memory_id );
		if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ] ) == SQLITE_ROW )
			mva_child_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 1 );

		// prep eps
		epmem_rit_prep_left_right( my_agent, memory_id, memory_id, epmem_rit_state_one );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 1, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 2, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3, memory_id );
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 4, memory_id );
		while ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ] ) == SQLITE_ROW )
		{			
			// e.id, i.parent_id, i.name, i.value
			child_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 0 );
			parent_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 1 );
			name = (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 2 );
			wme_type = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 4 );
			
			// make a symbol to represent the attribute name		
			attr = make_sym_constant( my_agent, const_cast<char *>( name ) );

			// must differentiate between mva and ep
			if ( child_id != mva_child_id )
			{
				// get a reference to the parent
				parent = ids.find( parent_id );
				if ( parent == ids.end() )
					parent = mva_ids.find( parent_id );

				// identifier = NULL, else attr->val
				if ( wme_type == IDENTIFIER_SYMBOL_TYPE )
				{
					value = make_new_identifier( my_agent, name[0], parent->second->id.level );				
					
					new_wme = add_input_wme( my_agent, parent->second, attr, value );
					new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
					state->id.epmem_info->epmem_wmes->push( new_wme );

					symbol_remove_ref( my_agent, value );

					ids[ child_id ] = value;
				}
				else
				{
					switch ( wme_type )
					{
						case INT_CONSTANT_SYMBOL_TYPE:
							value = make_int_constant( my_agent, sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
							break;

						case FLOAT_CONSTANT_SYMBOL_TYPE:
							value = make_float_constant( my_agent, sqlite3_column_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
							break;

						case SYM_CONSTANT_SYMBOL_TYPE:						
							value = make_sym_constant( my_agent, const_cast<char *>( (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) ) );
							break;
					}

					new_wme = add_input_wme( my_agent, parent->second, attr, value );
					new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
					state->id.epmem_info->epmem_wmes->push( new_wme );
				}
			}
			else
			{
				do
				{
					// get appropriate parent
					if ( sqlite3_column_type( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 2 ) == SQLITE_NULL )
						parent = ids.find( parent_id );
					else
						parent = mva_ids.find( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 2 ) );

					// identifier = NULL, else attr->val
					if ( wme_type == IDENTIFIER_SYMBOL_TYPE )
					{
						value = make_new_identifier( my_agent, name[0], parent->second->id.level );				
						
						new_wme = add_input_wme( my_agent, parent->second, attr, value );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						symbol_remove_ref( my_agent, value );

						if ( sqlite3_column_type( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 0 ) == SQLITE_NULL )
							ids[ child_id ] = value;
						else
							mva_ids[ sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 0 ) ] = value;
					}
					else
					{
						switch ( wme_type )
						{
							case INT_CONSTANT_SYMBOL_TYPE:
								value = make_int_constant( my_agent, sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
								break;

							case FLOAT_CONSTANT_SYMBOL_TYPE:
								value = make_float_constant( my_agent, sqlite3_column_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) );
								break;

							case SYM_CONSTANT_SYMBOL_TYPE:						
								value = make_sym_constant( my_agent, const_cast<char *>( (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ], 3 ) ) );
								break;
						}

						new_wme = add_input_wme( my_agent, parent->second, attr, value );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );
					}

					if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ] ) == SQLITE_ROW )
						mva_child_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ], 1 );
					else
						mva_child_id = EPMEM_MEMID_NONE;

				} while ( child_id == mva_child_id );
			}
		}
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_GET_EPISODE ] );
		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_MVA_GET_EP ] );

		epmem_rit_clear_left_right( my_agent );
	}
	else if ( mode == EPMEM_MODE_THREE )
	{
		std::map<epmem_node_id, Symbol *> ids;

		Symbol *parent = NULL;
		Symbol *attr = NULL;
		
		// first identifiers (i.e. reconstruct)
		ids[ 0 ] = retrieved_header;
		{
			epmem_node_id q0;
			const char *w;
			epmem_node_id q1;

			Symbol **value = NULL;

			std::map<epmem_node_id, Symbol *>::iterator id_p;
			std::queue<epmem_path *> stragglers;
			epmem_path *straggler;

			epmem_rit_prep_left_right( my_agent, memory_id, memory_id, epmem_rit_state_three[ EPMEM_RIT_STATE_RECONSTRUCT ] );
			
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 1, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 2, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 3, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 4, memory_id );
			while ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ] ) == SQLITE_ROW )
			{
				// q0, w, q1
				q0 = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 0 );
				w = (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 1 );
				q1 = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ], 2 );

				// get a reference to the parent
				id_p = ids.find( q0 );
				if ( id_p != ids.end() )
				{
					parent = id_p->second;

					// make a symbol to represent the attribute name		
					attr = make_sym_constant( my_agent, const_cast<char *>( w ) );

					value =& ids[ q1 ]; 
					if ( (*value) == NULL )
						(*value) = make_new_identifier( my_agent, w[0], parent->id.level );

					new_wme = add_input_wme( my_agent, parent, attr, (*value) );
					new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
					state->id.epmem_info->epmem_wmes->push( new_wme );

					symbol_remove_ref( my_agent, (*value) );
				}
				else
				{
					// out of order
					straggler = new epmem_path;
					straggler->q0 = q0;
					straggler->w = new std::string( w );
					straggler->q1 = q1;

					stragglers.push( straggler );
				}
			}
			sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_RECONSTRUCT ] );
			epmem_rit_clear_left_right( my_agent );

			// take care of any stragglers
			while ( !stragglers.empty() )
			{
				straggler = stragglers.front();
				stragglers.pop();

				// get a reference to the parent
				id_p = ids.find( straggler->q0 );
				if ( id_p != ids.end() )
				{
					parent = id_p->second;

					// make a symbol to represent the attribute name		
					attr = make_sym_constant( my_agent, const_cast<char *>( straggler->w->c_str() ) );

					value =& ids[ straggler->q1 ]; 
					if ( (*value) == NULL )
						(*value) = make_new_identifier( my_agent, straggler->w->at(0), parent->id.level );

					new_wme = add_input_wme( my_agent, parent, attr, (*value) );
					new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
					state->id.epmem_info->epmem_wmes->push( new_wme );

					symbol_remove_ref( my_agent, (*value) );

					delete straggler->w;
					delete straggler;
				}
				else
				{					
					stragglers.push( straggler );
				}
			}
		}

		// then features
		{
			epmem_node_id child_id;
			epmem_node_id parent_id;
			const char *name;
			long long wme_type;

			Symbol *value = NULL;
			
			epmem_rit_prep_left_right( my_agent, memory_id, memory_id, epmem_rit_state_three[ EPMEM_RIT_STATE_SEARCH ] );

			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 1, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 2, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 3, memory_id );
			sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 4, memory_id );
			while ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ] ) == SQLITE_ROW )
			{
				// f.child_id, f.parent_id, f.name, f.value, f.wme_type
				child_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 0 );
				parent_id = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 1 );
				name = (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 2 );
				wme_type = sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 4 );

				// get a reference to the parent
				parent = ids[ parent_id ];

				// make a symbol to represent the attribute name		
				attr = make_sym_constant( my_agent, const_cast<char *>( name ) );

				switch ( wme_type )
				{
					case INT_CONSTANT_SYMBOL_TYPE:
						value = make_int_constant( my_agent, sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 3 ) );
						break;

					case FLOAT_CONSTANT_SYMBOL_TYPE:
						value = make_float_constant( my_agent, sqlite3_column_double( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 3 ) );
						break;

					case SYM_CONSTANT_SYMBOL_TYPE:						
						value = make_sym_constant( my_agent, const_cast<char *>( (const char *) sqlite3_column_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ], 3 ) ) );
						break;
				}

				new_wme = add_input_wme( my_agent, parent, attr, value );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
				state->id.epmem_info->epmem_wmes->push( new_wme );
			}

			sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_GET_SEARCH ] );
			epmem_rit_clear_left_right( my_agent );
		}
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_NCB_RETRIEVAL );
	////////////////////////////////////////////////////////////////////////////
}

// returns the next valid temporal id
epmem_time_id epmem_next_episode( agent *my_agent, epmem_time_id memory_id )
{
	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_NEXT );
	////////////////////////////////////////////////////////////////////////////
	
	const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );
	epmem_time_id return_val = EPMEM_MEMID_NONE;
	
	if ( ( mode == EPMEM_MODE_ONE ) || ( mode == EPMEM_MODE_TWO ) ) 
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_NEXT_EPISODE ], 1, memory_id );
		if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_NEXT_EPISODE ] ) == SQLITE_ROW )
			return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_NEXT_EPISODE ], 0 ) );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_NEXT_EPISODE ] );
	}
	else if ( mode == EPMEM_MODE_THREE )
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_NEXT_EPISODE ], 1, memory_id );
		if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_NEXT_EPISODE ] ) == SQLITE_ROW )
			return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_NEXT_EPISODE ], 0 ) );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_NEXT_EPISODE ] );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_NEXT );
	////////////////////////////////////////////////////////////////////////////

	return return_val;
}

// returns the previous valid temporal id
epmem_time_id epmem_previous_episode( agent *my_agent, epmem_time_id memory_id )
{
	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_PREV );
	////////////////////////////////////////////////////////////////////////////
	
	const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );
	epmem_time_id return_val = EPMEM_MEMID_NONE;
	
	if ( ( mode == EPMEM_MODE_ONE ) || ( mode == EPMEM_MODE_TWO ) ) 
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_PREV_EPISODE ], 1, memory_id );
		if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_PREV_EPISODE ] ) == SQLITE_ROW )
			return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_PREV_EPISODE ], 0 ) );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_PREV_EPISODE ] );
	}
	else if ( mode == EPMEM_MODE_THREE )
	{
		sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_PREV_EPISODE ], 1, memory_id );
		if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_PREV_EPISODE ] ) == SQLITE_ROW )
			return_val = ( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_PREV_EPISODE ], 0 ) );

		sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_PREV_EPISODE ] );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_PREV );
	////////////////////////////////////////////////////////////////////////////
	
	return return_val;
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// Cue-Based Retrieval (epmem::cbr)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// quicky constructor
epmem_leaf_node *epmem_create_leaf_node( epmem_node_id leaf_id, double leaf_weight )
{
	epmem_leaf_node *newbie = new epmem_leaf_node();

	newbie->leaf_id = leaf_id;
	newbie->leaf_weight = leaf_weight;

	return newbie;
}

// reads all b-tree pointers in parallel
void epmem_incremental_row( agent *my_agent, epmem_range_query *stmts[2][2][3], epmem_time_id tops[2], int query_sizes[2], epmem_time_id &id, long long &ct, double &v, long long &updown, const unsigned int list )
{	
	// initialize variables
	id = tops[ list ];
	ct = 0;
	v = 0;
	updown = 0;

	int i, k, m;
	bool more_data;
	epmem_time_id next_id = EPMEM_MEMID_NONE;
	epmem_time_id new_top = EPMEM_MEMID_NONE;

	// identify lists that can possibly contribute to the current top
	for ( i=0; i<2; i++ )
	{
		if ( query_sizes[ i ] )
		{
			for ( k=0; k<3; k++ )
			{
				for ( m=0; m<query_sizes[ i ]; m++ )
				{
					// for each of these, grab new data till no longer good
					if ( stmts[ i ][ list ][ k ][ m ].val == id )
					{
						do
						{
							updown++;
							v += stmts[ i ][ list ][ k ][ m ].weight;
							ct += stmts[ i ][ list ][ k ][ m ].ct;
							
							more_data = ( epmem_exec_range_query( my_agent, &( stmts[ i ][ list ][ k ][ m ] ) ) == SQLITE_ROW );
							
							if ( more_data )
								next_id = sqlite3_column_int64( stmts[ i ][ list ][ k ][ m ].stmt, 0 );

						} while ( more_data && ( next_id == id ) );

						if ( more_data )
						{
							stmts[ i ][ list ][ k ][ m ].val = next_id;
							if ( next_id > new_top )
								new_top = next_id;
						}
						else
						{							
							sqlite3_finalize( stmts[ i ][ list ][ k ][ m ].stmt );							
							stmts[ i ][ list ][ k ][ m ].stmt = NULL;
							stmts[ i ][ list ][ k ][ m ].val = EPMEM_MEMID_NONE;
						}
					}
					else
					{
						if ( stmts[ i ][ list ][ k ][ m ].val > new_top )
							new_top = stmts[ i ][ list ][ k ][ m ].val;
					}
				}
			}
		}
	}

	if ( list == EPMEM_RANGE_START )
		updown = -updown;

	tops[ list ] = new_top;
}

// reads all b-tree pointers in parallel
void epmem_incremental_row3( agent *my_agent, epmem_range_query *stmts[2][2][3], epmem_time_id tops[2], int ambig_sizes[2], long long *point_groups[2], epmem_time_id &id, long long &ct, double &v, long long &updown, const unsigned int list )
{	
	// initialize variables
	id = tops[ list ];
	ct = 0;
	v = 0;
	updown = 0;

	int i, k, m;
	bool more_data, group_shift;
	epmem_time_id next_id = EPMEM_MEMID_NONE;
	epmem_time_id new_top = EPMEM_MEMID_NONE;

	// identify lists that can possibly contribute to the current top
	for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
	{
		if ( ambig_sizes[ i ] )
		{
			for ( k=EPMEM_RANGE_EP; k<=EPMEM_RANGE_POINT; k++ )
			{
				for ( m=0; m<ambig_sizes[ i ]; m++ )
				{
					// for each of these, grab new data till no longer good
					if ( stmts[ i ][ list ][ k ][ m ].val == id )
					{
						do
						{
							group_shift = false;		
							if ( list == EPMEM_RANGE_START )
								group_shift = ( !( --point_groups[ i ][ stmts[ i ][ list ][ k ][ m ].group ] ) );
							else
								group_shift = ( !( point_groups[ i ][ stmts[ i ][ list ][ k ][ m ].group ]++ ) );

							if ( group_shift )
							{
								updown++;
								v += stmts[ i ][ list ][ k ][ m ].weight;
								ct += stmts[ i ][ list ][ k ][ m ].ct;
							}
							
							more_data = ( epmem_exec_range_query( my_agent, &( stmts[ i ][ list ][ k ][ m ] ) ) == SQLITE_ROW );
							
							if ( more_data )
								next_id = sqlite3_column_int64( stmts[ i ][ list ][ k ][ m ].stmt, 0 );

						} while ( more_data && ( next_id == id ) );

						if ( more_data )
						{
							stmts[ i ][ list ][ k ][ m ].val = next_id;
							if ( next_id > new_top )
								new_top = next_id;
						}
						else
						{							
							sqlite3_finalize( stmts[ i ][ list ][ k ][ m ].stmt );							
							stmts[ i ][ list ][ k ][ m ].stmt = NULL;
							stmts[ i ][ list ][ k ][ m ].val = EPMEM_MEMID_NONE;
						}
					}
					else
					{
						if ( stmts[ i ][ list ][ k ][ m ].val > new_top )
							new_top = stmts[ i ][ list ][ k ][ m ].val;
					}
				}
			}
		}
	}

	if ( list == EPMEM_RANGE_START )
		updown = -updown;

	tops[ list ] = new_top;
}

// performs cue-based query
void epmem_process_query( agent *my_agent, Symbol *state, Symbol *query, Symbol *neg_query, std::vector<epmem_time_id> *prohibit, epmem_time_id before, epmem_time_id after )
{
	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_QUERY );
	////////////////////////////////////////////////////////////////////////////
	
	int len_query = 0, len_neg_query = 0;
	wme **wmes_query = NULL;
	if ( query != NULL )
		wmes_query = epmem_get_augs_of_id( my_agent, query, get_new_tc_number( my_agent ), &len_query );

	wme **wmes_neg_query = NULL;
	if ( neg_query != NULL )
		wmes_neg_query = epmem_get_augs_of_id( my_agent, neg_query, get_new_tc_number( my_agent ), &len_neg_query );

	if ( ( len_query != 0 ) || ( len_neg_query != 0 ) )
	{
		const long mode = epmem_get_parameter( my_agent, EPMEM_PARAM_MODE, EPMEM_RETURN_LONG );

		if ( !prohibit->empty() )
			std::sort( prohibit->begin(), prohibit->end() );		
		if ( ( mode == EPMEM_MODE_ONE ) || ( mode == EPMEM_MODE_TWO ) )
		{
			wme *new_wme;

			// get the leaf id's			
			std::list<epmem_leaf_node *> leaf_ids[2];
			std::list<epmem_leaf_node *>::iterator leaf_p;		
			std::vector<epmem_time_id>::iterator prohibit_p;

			{
				wme ***wmes;
				int len;
				
				std::queue<Symbol *> parent_syms;
				std::queue<epmem_node_id> parent_ids;		
				int tc = get_new_tc_number( my_agent );

				Symbol *parent_sym;
				epmem_node_id parent_id;
				unsigned long my_hash;

				int i, j;
				bool just_started;

				// initialize pos/neg lists
				for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
				{			
					switch ( i )
					{
						case EPMEM_NODE_POS:
							wmes = &wmes_query;
							len = len_query;					
							parent_syms.push( query );
							parent_ids.push( EPMEM_MEMID_ROOT );
							just_started = true;
							break;

						case EPMEM_NODE_NEG:
							wmes = &wmes_neg_query;
							len = len_neg_query;						
							parent_syms.push( neg_query );
							parent_ids.push( EPMEM_MEMID_ROOT );
							just_started = true;
							break;
					}
					
					while ( !parent_syms.empty() )
					{
						parent_sym = parent_syms.front();
						parent_syms.pop();

						parent_id = parent_ids.front();					
						parent_ids.pop();

						if ( !just_started )
							(*wmes) = epmem_get_augs_of_id( my_agent, parent_sym, tc, &len );
						else
							just_started = false;

						if ( (*wmes) != NULL )
						{
							for ( j=0; j<len; j++ )
							{
								// add to cue list
								state->id.epmem_info->cue_wmes->push_back( (*wmes)[ j ] );
								
								// find wme id							
								my_hash = epmem_hash_wme( (*wmes)[j] );
								if ( (*wmes)[j]->value->common.symbol_type != IDENTIFIER_SYMBOL_TYPE )
								{
									// hash=? AND parent_id=? AND name=? AND value=?
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 1, my_hash );
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 2, parent_id );
									sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 3, (const char *) (*wmes)[j]->attr->sc.name, -1, SQLITE_STATIC );
									switch( (*wmes)[j]->value->common.symbol_type )
									{
										case SYM_CONSTANT_SYMBOL_TYPE:
											sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, (const char *) (*wmes)[j]->value->sc.name, -1, SQLITE_STATIC );
											break;
								            
										case INT_CONSTANT_SYMBOL_TYPE:
			        						sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, (*wmes)[j]->value->ic.value );
											break;
							
										case FLOAT_CONSTANT_SYMBOL_TYPE:
			        						sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 4, (*wmes)[j]->value->fc.value );
											break;
									}
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 5, (*wmes)[j]->value->common.symbol_type );
									
									if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] ) == SQLITE_ROW )
										leaf_ids[i].push_back( epmem_create_leaf_node( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ], 0 ), wma_get_wme_activation( my_agent, (*wmes)[j] ) ) );
									
									sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID ] );
								}
								else
								{
									// hash=? AND parent_id=? AND name=? AND value IS NULL						
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 1, my_hash );
									sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 2, parent_id );
									sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 3, (const char *) (*wmes)[j]->attr->sc.name, -1, SQLITE_STATIC );

									if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] ) == SQLITE_ROW )
									{
										parent_syms.push( (*wmes)[j]->value );
										parent_ids.push( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ], 0 ) );
									}

									sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_ONE_FIND_ID_NULL ] );
								}
							}

							// free space from aug list
							free_memory( my_agent, (*wmes), MISCELLANEOUS_MEM_USAGE );
						}
					}
				}
			}
			
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_POS, leaf_ids[ EPMEM_NODE_POS ].size() );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_NEG, leaf_ids[ EPMEM_NODE_NEG ].size() );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_RET, 0 );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_CARD, 0 );

			// useful statistics
			int cue_sizes[2] = { leaf_ids[ EPMEM_NODE_POS ].size(), leaf_ids[ EPMEM_NODE_NEG ].size() };
			int cue_size = ( cue_sizes[ EPMEM_NODE_POS ] + cue_sizes[ EPMEM_NODE_NEG ] );
			int perfect_match = leaf_ids[ EPMEM_NODE_POS ].size();

			// only perform search if necessary
			if ( cue_size )
			{
				// perform incremental, integrated range search
				{				
					// variables to populate
					epmem_time_id king_id = EPMEM_MEMID_NONE;
					double king_score = -1000;
					unsigned long long king_cardinality = 0;

					// prepare queries				
					epmem_range_query *range_list[2][2][3] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };				
					int i, j, k, m;
					{
						const char *tail;
						int timer;
						epmem_time_id time_now = epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME ) - 1;
						int position;
						
						for ( i=0; i<2; i++ )
						{				
							if ( cue_sizes[ i ] )
							{						
								for ( j=0; j<2; j++ )
								{
									timer = ( ( i == EPMEM_NODE_POS )?( ( j == EPMEM_RANGE_START )?( EPMEM_TIMER_QUERY_POS_START_EP ):( EPMEM_TIMER_QUERY_POS_END_EP ) ):( ( j == EPMEM_RANGE_START )?( EPMEM_TIMER_QUERY_NEG_START_EP ):( EPMEM_TIMER_QUERY_NEG_END_EP ) ) );
									
									for ( k=0; k<3; k++ )
									{									
										range_list[ i ][ j ][ k ] = new epmem_range_query[ cue_sizes[ i ] ];

										m = 0;
										leaf_p = leaf_ids[i].begin();
										while ( leaf_p != leaf_ids[i].end() )
										{
											range_list[ i ][ j ][ k ][ m ].val = EPMEM_MEMID_NONE;
											range_list[ i ][ j ][ k ][ m ].timer = timer;											
											range_list[ i ][ j ][ k ][ m ].weight = ( ( i == EPMEM_NODE_POS )?( 1 ):( -1 ) ) * (*leaf_p)->leaf_weight;
											range_list[ i ][ j ][ k ][ m ].ct = ( ( i == EPMEM_NODE_POS )?( 1 ):( -1 ) );											

											sqlite3_prepare_v2( my_agent->epmem_db, epmem_range_queries[ i ][ j ][ k ], -1, &( range_list[ i ][ j ][ k ][ m ].stmt ), &tail );										

											// bind values
											position = 1;
											
											if ( ( k == EPMEM_RANGE_NOW ) && ( j == EPMEM_RANGE_END ) )
												sqlite3_bind_int64( range_list[ i ][ j ][ k ][ m ].stmt, position++, time_now );

											sqlite3_bind_int64( range_list[ i ][ j ][ k ][ m ].stmt, position, (*leaf_p)->leaf_id );											

											m++;
											leaf_p++;
										}

										timer++;
									}
								}
							}						
						}
					}

					// initialize lists
					epmem_time_id top_list_id[2] = { EPMEM_MEMID_NONE, EPMEM_MEMID_NONE };
					{
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{
							if ( cue_sizes[ i ] )
							{						
								for ( j=EPMEM_RANGE_START; j<=EPMEM_RANGE_END; j++ )
								{
									for ( k=EPMEM_RANGE_EP; k<=EPMEM_RANGE_POINT; k++ )
									{
										for ( m=0; m<cue_sizes[ i ]; m++ )
										{
											if ( epmem_exec_range_query( my_agent, &range_list[ i ][ j ][ k ][ m ] ) == SQLITE_ROW )
											{
												range_list[ i ][ j ][ k ][ m ].val = sqlite3_column_int64( range_list[ i ][ j ][ k ][ m ].stmt, 0 );
												if ( range_list[ i ][ j ][ k ][ m ].val > top_list_id[ j ] )
													top_list_id[ j ] = range_list[ i ][ j ][ k ][ m ].val;
											}
											else
											{
												sqlite3_finalize( range_list[ i ][ j ][ k ][ m ].stmt );
												range_list[ i ][ j ][ k ][ m ].stmt = NULL;
											}
										}
									}
								}
							}
						}				
					}
					
					if ( top_list_id[ EPMEM_RANGE_END ] != EPMEM_MEMID_NONE )
					{
						double balance = epmem_get_parameter( my_agent, (const long) EPMEM_PARAM_BALANCE );
						double balance_inv = 1 - balance;

						// dynamic programming stuff
						long long sum_ct = 0;
						double sum_v = 0;
						long long sum_updown = 0;

						// current pointer					
						epmem_time_id current_id = EPMEM_MEMID_NONE;
						long long current_ct = 0;
						double current_v = 0;
						long long current_updown = 0;
						epmem_time_id current_end;
						epmem_time_id current_valid_end;
						double current_score;

						// next pointers
						epmem_time_id start_id = EPMEM_MEMID_NONE;
						epmem_time_id end_id = EPMEM_MEMID_NONE;
						epmem_time_id *next_id;
						unsigned int next_list;	

						// prohibit pointer
						long long current_prohibit = ( ( (long long) prohibit->size() ) - 1 );
						
						// completion (allows for smart cut-offs later)
						bool done = false;

						// initialize current as last end
						// initialize next end
						epmem_incremental_row( my_agent, range_list, top_list_id, cue_sizes, current_id, current_ct, current_v, current_updown, EPMEM_RANGE_END );
						end_id = top_list_id[ EPMEM_RANGE_END ];
						
						// initialize next start					
						start_id = top_list_id[ EPMEM_RANGE_START ];

						do
						{
							// if both lists are finished, we are done
							if ( ( start_id == EPMEM_MEMID_NONE ) && ( end_id == EPMEM_MEMID_NONE ) )
							{
								done = true;
							}
							// if we are beyond a specified after, we are done
							else if ( ( after != EPMEM_MEMID_NONE ) && ( current_id <= after ) )
							{
								done = true;
							}
							// if one list finished, go to the other
							else if ( ( start_id == EPMEM_MEMID_NONE ) || ( end_id == EPMEM_MEMID_NONE ) )
							{
								next_list = ( ( start_id == EPMEM_MEMID_NONE )?( EPMEM_RANGE_END ):( EPMEM_RANGE_START ) );
							}
							// if neither list finished, we prefer the higher id (end in case of tie)
							else
							{
								next_list = ( ( start_id > end_id )?( EPMEM_RANGE_START ):( EPMEM_RANGE_END ) );
							}

							if ( !done )
							{
								// update sums
								sum_ct += current_ct;
								sum_v += current_v;
								sum_updown += current_updown;

								// update end
								current_end = ( ( next_list == EPMEM_RANGE_END )?( end_id + 1 ):( start_id ) );
								if ( before == EPMEM_MEMID_NONE )
									current_valid_end = current_id;
								else
									current_valid_end = ( ( current_id < before )?( current_id ):( before - 1 ) );
								
								while ( ( current_prohibit != -1 ) && ( current_valid_end >= current_end ) && ( current_valid_end <= (*prohibit)[ current_prohibit ] ) )
								{							
									if ( current_valid_end == (*prohibit)[ current_prohibit ] )
										current_valid_end--;

									current_prohibit--;
								}

								// if we are beyond before AND
								// we are in a range, compute score
								// for possible new king
								if ( ( current_valid_end >= current_end ) && ( sum_updown != 0 ) )
								{
									current_score = ( balance * sum_ct ) + ( balance_inv * sum_v );								
									
									// new king if no old king OR better score
									if ( ( king_id == EPMEM_MEMID_NONE ) || ( current_score > king_score ) )
									{
										king_id = current_valid_end;
										king_score = current_score;
										king_cardinality = sum_ct;

										if ( king_cardinality == perfect_match )
											done = true;
									}
								}

								if ( !done )
								{
									// based upon choice, update variables
									epmem_incremental_row( my_agent, range_list, top_list_id, cue_sizes, current_id, current_ct, current_v, current_updown, next_list );
									current_id = current_end - 1;
									current_ct *= ( ( next_list == EPMEM_RANGE_START )?( -1 ):( 1 ) );
									current_v *= ( ( next_list == EPMEM_RANGE_START )?( -1 ):( 1 ) );
									
									next_id = ( ( next_list == EPMEM_RANGE_START )?( &start_id ):( &end_id ) );
									(*next_id) = top_list_id[ next_list ];								
								}
							}

						} while ( !done );
					}

					// clean up
					{
						for ( i=0; i<2; i++ )
						{
							if ( cue_sizes[ i ] )
							{
								for ( j=0; j<2; j++ )						
								{
									for ( k=0; k<3; k++ )							
									{										
										for ( m=0; m<cue_sizes[ i ]; m++ )
										{
											if ( range_list[ i ][ j ][ k ][ m ].stmt != NULL )
												sqlite3_finalize( range_list[ i ][ j ][ k ][ m ].stmt );										
										}

										delete [] ( range_list[ i ][ j ][ k ] );
									}
								}
							}
						}
					}
				
					// place results in WM
					if ( king_id != EPMEM_MEMID_NONE )
					{						
						epmem_set_stat( my_agent, EPMEM_STAT_QRY_RET, king_id );
						epmem_set_stat( my_agent, EPMEM_STAT_QRY_CARD, king_cardinality );
						
						// status
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_success_symbol );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// match score
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_match_score_symbol, make_float_constant( my_agent, king_score ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// cue-size
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_cue_size_symbol, make_int_constant( my_agent, cue_size ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// normalized-match-score
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_normalized_match_score_symbol, make_float_constant( my_agent, ( king_score / cue_size ) ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// match-cardinality
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_match_cardinality_symbol, make_int_constant( my_agent, king_cardinality ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );					

						// actual memory
						epmem_install_memory( my_agent, state, king_id );
					}
					else
					{
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_failure_symbol );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );
					}
					
					{						
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{						
							leaf_p = leaf_ids[i].begin();
							while ( leaf_p != leaf_ids[i].end() )
							{
								delete (*leaf_p);

								leaf_p++;
							}						
						}
					}
				}			
			}
			else
			{				
				new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_failure_symbol );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
				state->id.epmem_info->epmem_wmes->push( new_wme );
			}
		}
		else if ( mode == EPMEM_MODE_THREE )
		{
			// get leaf nodes
			wme *new_wme;

			// get the leaf id's
			std::list<epmem_ambig_leaf_node *> leaf_ids[2];
			std::list<epmem_ambig_leaf_node *>::iterator leaf_p;			
			std::set<epmem_node_id>::iterator ambig_p;
			std::vector<epmem_time_id>::iterator prohibit_p;

			int ambig_sizes[2] = { 0, 0 };

			{
				wme ***wmes;
				int len;
				
				std::queue<Symbol *> parent_syms;
				std::queue<std::set<epmem_node_id> *> parent_ids;
				int tc = get_new_tc_number( my_agent );				
				
				Symbol *parent_sym;
				std::set<epmem_node_id> *parent_id;
				unsigned long my_hash;

				std::set<epmem_node_id> *id_identities;
				epmem_ambig_leaf_node *leaf_identities;

				int i, j;
				bool just_started;

				for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
				{			
					parent_id = new std::set<epmem_node_id>();
					parent_id->insert( EPMEM_MEMID_ROOT );
					
					// init
					switch ( i )
					{
						case EPMEM_NODE_POS:
							wmes = &wmes_query;
							len = len_query;					
							parent_syms.push( query );
							parent_ids.push( parent_id );
							just_started = true;
							break;

						case EPMEM_NODE_NEG:
							wmes = &wmes_neg_query;
							len = len_neg_query;						
							parent_syms.push( neg_query );							
							parent_ids.push( parent_id );
							just_started = true;
							break;
					}
					parent_id = NULL;

					// depth-first search
					while ( !parent_syms.empty() )
					{
						// pop current info
						parent_sym = parent_syms.front();
						parent_syms.pop();

						parent_id = parent_ids.front();					
						parent_ids.pop();

						if ( !just_started )
							(*wmes) = epmem_get_augs_of_id( my_agent, parent_sym, tc, &len );
						else
							just_started = false;

						if ( (*wmes) != NULL )
						{
							for ( j=0; j<len; j++ )
							{
								// add to cue list
								state->id.epmem_info->cue_wmes->push_back( (*wmes)[ j ] );

								// find wme id							
								my_hash = epmem_hash_wme( (*wmes)[j] );
								if ( (*wmes)[j]->value->common.symbol_type == IDENTIFIER_SYMBOL_TYPE )
								{
									id_identities = new std::set<epmem_node_id>();

									for ( ambig_p=parent_id->begin(); ambig_p!=parent_id->end(); ambig_p++ )
									{
										// q0=? AND name=?
										sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ], 1, (*ambig_p) );
										sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ], 2, (const char *) (*wmes)[j]->attr->sc.name, -1, SQLITE_STATIC );

										while ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ] ) == SQLITE_ROW )											
											id_identities->insert( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ], 0 ) );											

										sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_PATH ] );
									}

									if ( id_identities->size() )
									{
										parent_syms.push( (*wmes)[j]->value );
										parent_ids.push( id_identities );
									}
									else
									{
										delete id_identities;
									}
								}
								else
								{
									leaf_identities = new epmem_ambig_leaf_node();
									leaf_identities->leaf_ids = new std::set<epmem_node_id>();

									for ( ambig_p=parent_id->begin(); ambig_p!=parent_id->end(); ambig_p++ )
									{
										// parent_id=? AND hash=? AND name=? AND value=? AND wme_type=?
										sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 1, (*ambig_p) );
										sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 2, my_hash );										
										sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 3, (const char *) (*wmes)[j]->attr->sc.name, -1, SQLITE_STATIC );
										switch( (*wmes)[j]->value->common.symbol_type )
										{
											case SYM_CONSTANT_SYMBOL_TYPE:
												sqlite3_bind_text( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, (const char *) (*wmes)[j]->value->sc.name, -1, SQLITE_STATIC );
												break;
									            
											case INT_CONSTANT_SYMBOL_TYPE:
			        							sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, (*wmes)[j]->value->ic.value );
												break;
								
											case FLOAT_CONSTANT_SYMBOL_TYPE:
			        							sqlite3_bind_double( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 4, (*wmes)[j]->value->fc.value );
												break;
										}
										sqlite3_bind_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 5, (*wmes)[j]->value->common.symbol_type );
										
										if ( sqlite3_step( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ] ) == SQLITE_ROW )
											leaf_identities->leaf_ids->insert( sqlite3_column_int64( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ], 0 ) );
										
										sqlite3_reset( my_agent->epmem_statements[ EPMEM_STMT_THREE_FIND_FEATURE ] );
									}

									// if the leaf was valid, add to list of ids
									if ( leaf_identities->leaf_ids->size() )
									{
										leaf_identities->leaf_weight = wma_get_wme_activation( my_agent, (*wmes)[j] );
										leaf_ids[i].push_back( leaf_identities );

										ambig_sizes[i] += leaf_identities->leaf_ids->size();
									}
									else
									{
										delete leaf_identities->leaf_ids;
										delete leaf_identities;
									}
								}
							}

							// free space from aug list
							free_memory( my_agent, (*wmes), MISCELLANEOUS_MEM_USAGE );
						}

						// release memory
						delete parent_id;
					}
				}
			}
			
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_POS, leaf_ids[ EPMEM_NODE_POS ].size() );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_NEG, leaf_ids[ EPMEM_NODE_NEG ].size() );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_RET, 0 );
			epmem_set_stat( my_agent, EPMEM_STAT_QRY_CARD, 0 );

			// useful statistics
			int cue_sizes[2] = { leaf_ids[ EPMEM_NODE_POS ].size(), leaf_ids[ EPMEM_NODE_NEG ].size() };
			int cue_size = ( cue_sizes[ EPMEM_NODE_POS ] + cue_sizes[ EPMEM_NODE_NEG ] );
			int perfect_match = leaf_ids[ EPMEM_NODE_POS ].size();

			// only perform search if necessary
			if ( cue_size )
			{
				// perform incremental, integrated range search
				{
					// variables to populate
					epmem_time_id king_id = EPMEM_MEMID_NONE;
					double king_score = -1000;
					unsigned long long king_cardinality = 0;

					// prepare queries
					epmem_range_query *range_list[2][2][3] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };				
					int i, j, k, m;
					{
						const char *tail;
						int timer, group;
						epmem_time_id time_now = epmem_get_stat( my_agent, (const long) EPMEM_STAT_TIME ) - 1;
						int position;
						
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{				
							if ( cue_sizes[ i ] )
							{						
								for ( j=EPMEM_RANGE_START; j<=EPMEM_RANGE_END; j++ )
								{
									timer = ( ( i == EPMEM_NODE_POS )?( ( j == EPMEM_RANGE_START )?( EPMEM_TIMER_QUERY_POS_START_EP ):( EPMEM_TIMER_QUERY_POS_END_EP ) ):( ( j == EPMEM_RANGE_START )?( EPMEM_TIMER_QUERY_NEG_START_EP ):( EPMEM_TIMER_QUERY_NEG_END_EP ) ) );
									
									for ( k=EPMEM_RANGE_EP; k<=EPMEM_RANGE_POINT; k++ )
									{									
										range_list[ i ][ j ][ k ] = new epmem_range_query[ ambig_sizes[ i ] ];
										
										group = 0;
										m = 0;
										for ( leaf_p=leaf_ids[i].begin(); leaf_p!=leaf_ids[i].end(); leaf_p++ )
										{										
											for ( ambig_p=(*leaf_p)->leaf_ids->begin(); ambig_p!=(*leaf_p)->leaf_ids->end(); ambig_p++ )
											{
												range_list[ i ][ j ][ k ][ m ].val = EPMEM_MEMID_NONE;
												range_list[ i ][ j ][ k ][ m ].timer = timer;
												range_list[ i ][ j ][ k ][ m ].weight = ( ( i == EPMEM_NODE_POS )?( 1 ):( -1 ) ) * (*leaf_p)->leaf_weight;
												range_list[ i ][ j ][ k ][ m ].ct = ( ( i == EPMEM_NODE_POS )?( 1 ):( -1 ) );
												range_list[ i ][ j ][ k ][ m ].group = group;

												sqlite3_prepare_v2( my_agent->epmem_db, epmem_range_queries3[ i ][ j ][ k ], -1, &( range_list[ i ][ j ][ k ][ m ].stmt ), &tail );

												// bind values
												position = 1;
												
												if ( ( k == EPMEM_RANGE_NOW ) && ( j == EPMEM_RANGE_END ) )
													sqlite3_bind_int64( range_list[ i ][ j ][ k ][ m ].stmt, position++, time_now );

												sqlite3_bind_int64( range_list[ i ][ j ][ k ][ m ].stmt, position, (*ambig_p) );
												
												m++;
											}										

											group++;
										}

										timer++;
									}
								}
							}						
						}
					}

					// initialize lists
					epmem_time_id top_list_id[2] = { EPMEM_MEMID_NONE, EPMEM_MEMID_NONE };
					{
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{
							if ( cue_sizes[ i ] )
							{						
								for ( j=EPMEM_RANGE_START; j<=EPMEM_RANGE_END; j++ )
								{
									for ( k=EPMEM_RANGE_EP; k<=EPMEM_RANGE_POINT; k++ )
									{
										for ( m=0; m<ambig_sizes[ i ]; m++ )
										{
											if ( epmem_exec_range_query( my_agent, &range_list[ i ][ j ][ k ][ m ] ) == SQLITE_ROW )
											{
												range_list[ i ][ j ][ k ][ m ].val = sqlite3_column_int64( range_list[ i ][ j ][ k ][ m ].stmt, 0 );
												if ( range_list[ i ][ j ][ k ][ m ].val > top_list_id[ j ] )
													top_list_id[ j ] = range_list[ i ][ j ][ k ][ m ].val;
											}
											else
											{
												sqlite3_finalize( range_list[ i ][ j ][ k ][ m ].stmt );
												range_list[ i ][ j ][ k ][ m ].stmt = NULL;
											}
										}
									}
								}
							}
						}				
					}

					// range search
					if ( top_list_id[ EPMEM_RANGE_END ] != EPMEM_MEMID_NONE )
					{
						double balance = epmem_get_parameter( my_agent, (const long) EPMEM_PARAM_BALANCE );
						double balance_inv = 1 - balance;

						// dynamic programming stuff
						long long sum_ct = 0;
						double sum_v = 0;
						long long sum_updown = 0;

						// current pointer					
						epmem_time_id current_id = EPMEM_MEMID_NONE;
						long long current_ct = 0;
						double current_v = 0;
						long long current_updown = 0;
						epmem_time_id current_end;
						epmem_time_id current_valid_end;
						double current_score;

						// next pointers
						epmem_time_id start_id = EPMEM_MEMID_NONE;
						epmem_time_id end_id = EPMEM_MEMID_NONE;
						epmem_time_id *next_id;
						unsigned int next_list;	

						// prohibit pointer
						long long current_prohibit = ( ( (long long) prohibit->size() ) - 1 );
						
						// completion (allows for smart cut-offs later)
						bool done = false;

						// initialize group counters
						long long *point_groups[2];
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{
							point_groups[i] = new long long[ cue_sizes[i] ];
							for ( m=0; m<cue_sizes[i]; m++ )
								point_groups[i][m] = 0;
						}

						// initialize current as last end
						// initialize next end
						epmem_incremental_row3( my_agent, range_list, top_list_id, ambig_sizes, point_groups, current_id, current_ct, current_v, current_updown, EPMEM_RANGE_END );
						end_id = top_list_id[ EPMEM_RANGE_END ];
						
						// initialize next start					
						start_id = top_list_id[ EPMEM_RANGE_START ];

						do
						{
							// if both lists are finished, we are done
							if ( ( start_id == EPMEM_MEMID_NONE ) && ( end_id == EPMEM_MEMID_NONE ) )
							{
								done = true;
							}
							// if we are beyond a specified after, we are done
							else if ( ( after != EPMEM_MEMID_NONE ) && ( current_id <= after ) )
							{
								done = true;
							}
							// if one list finished, go to the other
							else if ( ( start_id == EPMEM_MEMID_NONE ) || ( end_id == EPMEM_MEMID_NONE ) )
							{
								next_list = ( ( start_id == EPMEM_MEMID_NONE )?( EPMEM_RANGE_END ):( EPMEM_RANGE_START ) );
							}
							// if neither list finished, we prefer the higher id (end in case of tie)
							else
							{
								next_list = ( ( start_id > end_id )?( EPMEM_RANGE_START ):( EPMEM_RANGE_END ) );
							}

							if ( !done )
							{
								// update sums
								sum_ct += current_ct;
								sum_v += current_v;
								sum_updown += current_updown;

								// update end
								current_end = ( ( next_list == EPMEM_RANGE_END )?( end_id + 1 ):( start_id ) );
								if ( before == EPMEM_MEMID_NONE )
									current_valid_end = current_id;
								else
									current_valid_end = ( ( current_id < before )?( current_id ):( before - 1 ) );
								
								while ( ( current_prohibit != -1 ) && ( current_valid_end >= current_end ) && ( current_valid_end <= (*prohibit)[ current_prohibit ] ) )
								{							
									if ( current_valid_end == (*prohibit)[ current_prohibit ] )
										current_valid_end--;

									current_prohibit--;
								}

								// if we are beyond before AND
								// we are in a range, compute score
								// for possible new king
								if ( ( current_valid_end >= current_end ) && ( sum_updown != 0 ) )
								{
									current_score = ( balance * sum_ct ) + ( balance_inv * sum_v );								
									
									// new king if no old king OR better score
									if ( ( king_id == EPMEM_MEMID_NONE ) || ( current_score > king_score ) )
									{
										king_id = current_valid_end;
										king_score = current_score;
										king_cardinality = sum_ct;

										if ( king_cardinality == perfect_match )
											done = true;
									}
								}

								if ( !done )
								{
									// based upon choice, update variables
									epmem_incremental_row3( my_agent, range_list, top_list_id, ambig_sizes, point_groups, current_id, current_ct, current_v, current_updown, next_list );
									current_id = current_end - 1;
									current_ct *= ( ( next_list == EPMEM_RANGE_START )?( -1 ):( 1 ) );
									current_v *= ( ( next_list == EPMEM_RANGE_START )?( -1 ):( 1 ) );
									
									next_id = ( ( next_list == EPMEM_RANGE_START )?( &start_id ):( &end_id ) );
									(*next_id) = top_list_id[ next_list ];								
								}
							}

						} while ( !done );
					}

					// clean up queries
					{
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{
							if ( cue_sizes[ i ] )
							{
								for ( j=EPMEM_RANGE_START; j<=EPMEM_RANGE_END; j++ )						
								{
									for ( k=EPMEM_RANGE_EP; k<=EPMEM_RANGE_POINT; k++ )							
									{										
										for ( m=0; m<ambig_sizes[ i ]; m++ )
										{
											if ( range_list[ i ][ j ][ k ][ m ].stmt != NULL )
												sqlite3_finalize( range_list[ i ][ j ][ k ][ m ].stmt );										
										}

										delete [] ( range_list[ i ][ j ][ k ] );
									}
								}
							}
						}
					}

					// place results in WM
					if ( king_id != EPMEM_MEMID_NONE )
					{						
						epmem_set_stat( my_agent, EPMEM_STAT_QRY_RET, king_id );
						epmem_set_stat( my_agent, EPMEM_STAT_QRY_CARD, king_cardinality );
						
						// status
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_success_symbol );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// match score
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_match_score_symbol, make_float_constant( my_agent, king_score ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// cue-size
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_cue_size_symbol, make_int_constant( my_agent, cue_size ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// normalized-match-score
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_normalized_match_score_symbol, make_float_constant( my_agent, ( king_score / cue_size ) ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );

						// match-cardinality
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_match_cardinality_symbol, make_int_constant( my_agent, king_cardinality ) );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );					

						// actual memory
						epmem_install_memory( my_agent, state, king_id );
					}
					else
					{
						new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_failure_symbol );
						new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
						state->id.epmem_info->epmem_wmes->push( new_wme );
					}

					// clean leaf_ids
					{						
						for ( i=EPMEM_NODE_POS; i<=EPMEM_NODE_NEG; i++ )
						{						
							for ( leaf_p=leaf_ids[i].begin(); leaf_p!=leaf_ids[i].end(); leaf_p++ )
							{
								delete (*leaf_p)->leaf_ids;
								delete (*leaf_p);								
							}
						}
					}
				}
				
				/*
				// perform incremental, integrated range search
				{				
									
				}
				*/
			}
			else
			{				
				new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_failure_symbol );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
				state->id.epmem_info->epmem_wmes->push( new_wme );
			}
		}
	}
	else
	{
		wme *new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_bad_cmd_symbol );
		new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );
		state->id.epmem_info->epmem_wmes->push( new_wme );

		free_memory( my_agent, wmes_query, MISCELLANEOUS_MEM_USAGE );
		free_memory( my_agent, wmes_neg_query, MISCELLANEOUS_MEM_USAGE );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_QUERY );
	////////////////////////////////////////////////////////////////////////////
}


//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// High-Level Functions (epmem::high)
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// use trigger/force settings to automatically
// record a new episode
void epmem_consider_new_episode( agent *my_agent )
{	
	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_TRIGGER );
	////////////////////////////////////////////////////////////////////////////
	
	const long force = epmem_get_parameter( my_agent, EPMEM_PARAM_FORCE, EPMEM_RETURN_LONG );
	bool new_memory = false;
	
	if ( force == EPMEM_FORCE_OFF )
	{
		const long trigger = epmem_get_parameter( my_agent, EPMEM_PARAM_TRIGGER, EPMEM_RETURN_LONG );
		
		if ( trigger == EPMEM_TRIGGER_OUTPUT )
		{
			slot *s;
			wme *w;
			Symbol *ol = my_agent->io_header_output;
			unsigned long wme_count = 0;
				
			// examine all commands on the output-link for any
			// that appeared since last memory was recorded
			for ( s = ol->id.slots; s != NIL; s = s->next )
			{
				for ( w = s->wmes; w != NIL; w = w->next )
				{
					wme_count++;
					
					if ( w->timetag > my_agent->bottom_goal->id.epmem_info->last_ol_time )
					{
						new_memory = true;
						my_agent->bottom_goal->id.epmem_info->last_ol_time = w->timetag; 
					}
				}
			}

			if ( my_agent->bottom_goal->id.epmem_info->last_ol_count != wme_count )
			{
				new_memory = true;
				my_agent->bottom_goal->id.epmem_info->last_ol_count = wme_count;
			}
		}
		else if ( trigger == EPMEM_TRIGGER_DC )
		{
			new_memory = true;
		}
		else if ( trigger == EPMEM_TRIGGER_NONE )
		{
			new_memory = false;
		}
	}	
	else
	{
		new_memory = ( force == EPMEM_FORCE_REMEMBER );
		
		epmem_set_parameter( my_agent, (const long) EPMEM_PARAM_FORCE, (const long) EPMEM_FORCE_OFF );
	}

	////////////////////////////////////////////////////////////////////////////
	epmem_stop_timer( my_agent, EPMEM_TIMER_TRIGGER );
	////////////////////////////////////////////////////////////////////////////

	if ( new_memory )
		epmem_new_episode( my_agent );
}

// implements the Soar-EpMem API
void epmem_respond_to_cmd( agent *my_agent )
{	
	// if this is before the first episode, initialize db components	
	if ( my_agent->epmem_db_status == -1 )
		epmem_init_db( my_agent );
	
	// respond to query only if db is properly initialized
	if ( my_agent->epmem_db_status != SQLITE_OK )
		return;

	////////////////////////////////////////////////////////////////////////////
	epmem_start_timer( my_agent, EPMEM_TIMER_API );
	////////////////////////////////////////////////////////////////////////////
	
	// start at the bottom and work our way up
	// (could go in the opposite direction as well)
	Symbol *state = my_agent->bottom_goal;

	wme **wmes;	
	const char *attr_name;
	int len;
	int i;

	epmem_time_id retrieve;
	bool next, previous;
	Symbol *query;
	Symbol *neg_query;
	std::vector<epmem_time_id> *prohibit;
	epmem_time_id before, after;
	bool good_cue;
	int path;

	slot *s;
	wme *w;
	Symbol *epmem_cmd;
	unsigned long wme_count;
	bool new_cue;

	while ( state != NULL )
	{
		// make sure this state has had some sort of change to the cmd
		new_cue = false;
		wme_count = 0;
		{
			epmem_cmd = state->id.epmem_cmd_header;
				
			// examine all entries on the cmd header
			// that appeared since last cue was encountered
			for ( s = epmem_cmd->id.slots; s != NIL; s = s->next )
			{
				for ( w = s->wmes; w != NIL; w = w->next )
				{
					wme_count++;
					
					if ( w->timetag > state->id.epmem_info->last_cmd_time )
					{
						new_cue = true;
						state->id.epmem_info->last_cmd_time = w->timetag; 
					}
				}
			}

			if ( state->id.epmem_info->last_cmd_count != wme_count )
			{				
				state->id.epmem_info->last_cmd_count = wme_count;

				if ( wme_count != 0 )
					new_cue = true;
				else
					epmem_clear_result( my_agent, state );
			}
		}
		
		if ( new_cue )
		{		
			// clear old cue
			state->id.epmem_info->cue_wmes->clear();
			
			// initialize command vars
			retrieve = EPMEM_MEMID_NONE;
			next = false;
			previous = false;
			query = NULL;
			neg_query = NULL;
			prohibit = new std::vector<epmem_time_id>();
			before = EPMEM_MEMID_NONE;
			after = EPMEM_MEMID_NONE;
			good_cue = true;
			path = 0;
			
			// get all top-level symbols
			wmes = epmem_get_augs_of_id( my_agent, state->id.epmem_cmd_header, get_new_tc_number( my_agent ), &len );

			// process top-level symbols
			for ( i=0; i<len; i++ )
			{
				if ( good_cue )
				{
					// get attribute name
					attr_name = (const char *) wmes[ i ]->attr->sc.name;

					if ( !strcmp( attr_name, "retrieve" ) )
					{
						if ( ( wmes[ i ]->value->ic.common_symbol_info.symbol_type == INT_CONSTANT_SYMBOL_TYPE ) &&
							 ( path == 0 ) && 
							 ( wmes[ i ]->value->ic.value > 0 ) )
						{
							retrieve = wmes[ i ]->value->ic.value;
							path = 1;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "next" ) )
					{
						if ( ( wmes[ i ]->value->id.common_symbol_info.symbol_type == IDENTIFIER_SYMBOL_TYPE ) &&						 
							 ( path == 0 ) )
						{
							next = true;
							path = 2;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "previous" ) )
					{
						if ( ( wmes[ i ]->value->id.common_symbol_info.symbol_type == IDENTIFIER_SYMBOL_TYPE ) &&						 
							 ( path == 0 ) )
						{
							previous = true;
							path = 2;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "query" ) )
					{
						if ( ( wmes[ i ]->value->id.common_symbol_info.symbol_type == IDENTIFIER_SYMBOL_TYPE ) &&						 
							 ( ( path == 0 ) || ( path == 3 ) ) &&
							 ( query == NULL ) )

						{
							query = wmes[ i ]->value;
							path = 3;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "neg-query" ) )
					{
						if ( ( wmes[ i ]->value->id.common_symbol_info.symbol_type == IDENTIFIER_SYMBOL_TYPE ) &&						 
							 ( ( path == 0 ) || ( path == 3 ) ) &&
							 ( neg_query == NULL ) )

						{
							neg_query = wmes[ i ]->value;
							path = 3;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "before" ) )
					{
						if ( ( wmes[ i ]->value->ic.common_symbol_info.symbol_type == INT_CONSTANT_SYMBOL_TYPE ) &&						 
							 ( ( path == 0 ) || ( path == 3 ) ) )
						{
							before = wmes[ i ]->value->ic.value;
							path = 3;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "after" ) )
					{
						if ( ( wmes[ i ]->value->ic.common_symbol_info.symbol_type == INT_CONSTANT_SYMBOL_TYPE ) &&						 
							 ( ( path == 0 ) || ( path == 3 ) ) )
						{
							after = wmes[ i ]->value->ic.value;
							path = 3;
						}
						else
							good_cue = false;
					}
					else if ( !strcmp( attr_name, "prohibit" ) )
					{
						if ( ( wmes[ i ]->value->ic.common_symbol_info.symbol_type == INT_CONSTANT_SYMBOL_TYPE ) &&						 
							 ( ( path == 0 ) || ( path == 3 ) ) )
						{
							prohibit->push_back( wmes[ i ]->value->ic.value );
							path = 3;
						}
						else
							good_cue = false;
					}
					else
						good_cue = false;
				}
			}

			// if on path 3 must have query/neg-query
			if ( ( path == 3 ) && ( query == NULL ) && ( neg_query == NULL ) )
				good_cue = false;

			// must be on a path
			if ( path == 0 )
				good_cue = false;

			////////////////////////////////////////////////////////////////////////////
			epmem_stop_timer( my_agent, EPMEM_TIMER_API );
			////////////////////////////////////////////////////////////////////////////

			if ( good_cue )
			{
				// retrieve
				if ( path == 1 )
				{				
					epmem_clear_result( my_agent, state );
					epmem_install_memory( my_agent, state, retrieve );					
				}
				// previous or next
				else if ( path == 2 )
				{
					epmem_clear_result( my_agent, state );
					epmem_install_memory( my_agent, state, ( ( next )?( epmem_next_episode( my_agent, state->id.epmem_info->last_memory ) ):( epmem_previous_episode( my_agent, state->id.epmem_info->last_memory ) ) ) );
				}
				// query
				else if ( path == 3 )
				{
					epmem_clear_result( my_agent, state );
					epmem_process_query( my_agent, state, query, neg_query, prohibit, before, after );
				}
			}
			else
			{
				epmem_clear_result( my_agent, state );
				
				wme *new_wme = add_input_wme( my_agent, state->id.epmem_result_header, my_agent->epmem_status_symbol, my_agent->epmem_bad_cmd_symbol );
				new_wme->preference = epmem_make_fake_preference( my_agent, state, new_wme );				
				state->id.epmem_info->epmem_wmes->push( new_wme );
			}

			// free prohibit list
			delete prohibit;

			// free space from aug list
			free_memory( my_agent, wmes, MISCELLANEOUS_MEM_USAGE );
		}

		state = state->id.higher_goal;
	}
}

// grand central of epmem
void epmem_go( agent *my_agent )
{
	epmem_start_timer( my_agent, EPMEM_TIMER_TOTAL );
	
	if ( !epmem_in_transaction( my_agent ) )
		epmem_transaction_begin( my_agent );
	
	epmem_consider_new_episode( my_agent );
	epmem_respond_to_cmd( my_agent );

	if ( !epmem_in_transaction( my_agent ) )
		epmem_transaction_end( my_agent, true );

	epmem_stop_timer( my_agent, EPMEM_TIMER_TOTAL );
}
