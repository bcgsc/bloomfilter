/*
 * BinTests.cpp
 * Unit Tests for Bin class
 *  Created on: July 24, 2019
 *      Author: Johnathan Wong
 */

/* automatically create main() function to run tests */
#define CATCH_CONFIG_MAIN

/* lightweight unit test framework */
#include "Bin.hpp"
#include "vendor/catch.hpp"

#include <assert.h>
#include <vector>

using namespace std;
typedef uint64_t T;

TEST_CASE("test fixture", "[Bin]")
{
	/*
	 * NOTES:
	 * - The SECTION blocks below are separate tests that share the
	 * same setup code
	 * - The common setup code is _re-run_ before each SECTION
	 * - In unit test terminology, this type of setup is known as a
	 * "test fixture"
	 * - See
	 * https://github.com/philsquared/Catch/blob/master/docs/tutorial.md#test-cases-and-sections for
	 * details
	 */

	/* START COMMON SETUP CODE */
	// 56 0s 00 11 11 11 0b
	std::vector<uint64_t> vect(2, 0);
	vect[0] = 63;
	vect[1] = 2;

	/* END COMMON SETUP CODE */

	SECTION("opearator cast overload")
	{
		// Check 00 11 11 [11] 0b = 3
		uint64_t val = Bin(vect[0], 2, 0, (T)3);
		std::cout << val << std::endl;
		assert(val == 3);
		assert(Bin(vect[0], 2, 0, (T)3) == 3);
		// Check [00] 11 11 11 0b = 0
		val = Bin(vect[0], 2, 3, (T)3);
		assert(val == 0);
		assert(Bin(vect[0], 2, 3, (T)3) == 0);
	}

	SECTION("opearator= overload")
	{
		assert(Bin(vect[0], 2, 3, (T)3) == 0);
		// Set [00] 11 11 11 0b to 3
		Bin(vect[0], 2, 3, (T)3) = 3;
		// Check [11 11 11 11 0b] to 255
		assert(vect[0] == 255);

		Bin bin(vect[1], 2, 0, (T)3);
		assert(bin == 2);
		// Set 00 00 00 [10] 0b to 3 using another Bin object
		bin = Bin(vect[0], 2, 2, (T)3);
		assert(vect[1] == 3);
	}

} /* end test fixture */
