// Prints Hello, world!
// By Ravi Netravali <ravinet@mit.edu>

#include <iostream>
#include <cstdlib>
#include "config.h"

using namespace std;

int main( void )
{
    cout << "Welcome to " << PACKAGE_STRING << endl << endl; 
    cout << "Hello, world!" << endl;
    return EXIT_SUCCESS;
}
