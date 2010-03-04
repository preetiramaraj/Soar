#include "portability.h"

#include <cppunit/TestRunner.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestResultCollector.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/BriefTestProgressListener.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/CompilerOutputter.h>
#include <ctime>

#include "simplelistener.h"
#include "misc.h"

bool g_Cancel = false;

#ifdef _WIN32
BOOL WINAPI handle_ctrlc( DWORD dwCtrlType )
{
	if ( dwCtrlType == CTRL_C_EVENT )
	{
		g_Cancel = true;
		return TRUE;
	}

	return FALSE;
}
#endif // _WIN32

bool g_NoRemote = false;

int main( int argc, char** argv )
{
#ifdef _WIN32
	//_crtBreakAlloc = 2168;
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF ); 
	SetConsoleCtrlHandler( handle_ctrlc, TRUE );
#endif // _WIN32

	for (int index = 1; index < argc; ++index) {
		std::string argument(argv[index]);
		if (argument == "--listener") {
            SimpleListener simpleListener( 600 );
            return simpleListener.run();

		} else if (argument == "--noremote") {
			std::cout << "Running tests without remote." << std::endl;
			g_NoRemote = true;
		} else {
			std::cerr << "Unknown argument " << argument << " ignored." << std::endl;
		}
	}

	srand( static_cast<unsigned>( time( NULL ) ) );

	//--- Create the event manager and test controller
	CPPUNIT_NS::TestResult controller;

	//--- Add a listener that colllects test result
	CPPUNIT_NS::TestResultCollector result;
	controller.addListener( &result );        

	//--- Add a listener that print dots as test run.
	CPPUNIT_NS::BriefTestProgressListener progress;
	controller.addListener( &progress );      

	//--- Add the top suite to the test runner
	CPPUNIT_NS::TestRunner runner;
	runner.addTest( CPPUNIT_NS::TestFactoryRegistry::getRegistry().makeTest() );
	runner.run( controller );

	CPPUNIT_NS::CompilerOutputter outputter( &result, std::cerr );
	outputter.write();                      

	return result.wasSuccessful() ? 0 : 1;
}
