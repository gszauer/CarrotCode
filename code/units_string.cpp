#include "strings.h"

int main() {
    
    RunUnitTestsStrInit();
    RunUnitTestsStrU8();
    RunUnitTestsStrU16();
    RunUnitTestsStrU32();
    RunUnitTestsStrConvert();

    PrintUnitTestResults(); // Let us know if any tests failed. Probably print expected and actual test values (and if it failed or not) while the test is running as well.

    return 0;
}