/*****************************************************
             PROJECT  : MATT
             VERSION  : 0.1.0-dev
             DATE     : 01/2014
             AUTHOR   : Valat Sébastien
             LICENSE  : CeCILL-C
*****************************************************/

/********************  HEADERS  *********************/
#include <gtest/gtest.h>
#include <core/ELFReader.hpp>
#include <common/SimpleAllocator.hpp>

/***************** USING NAMESPACE ******************/
using namespace MATT;

/*******************  FUNCTION  *********************/
TEST(ElfReader,constructor)
{
	ElfReader reader(TEST_BIN_DIR "/simple-case-no-finstr");
}

/*******************  FUNCTION  *********************/
bool hasVariable(ElfGlobalVariableVector & vars,std::string name,size_t size,bool tls)
{
	for (ElfGlobalVariableVector::const_iterator it = vars.begin() ; it != vars.end() ; ++it)
	{
		//fprintf(stderr,"%s -- %lu -- %d\n",it->name.c_str(),it->size,it->tls);
		if (it->name == name && it->size == size && it->tls == tls)
			return true;
	}
	
	return false;
}

/*******************  FUNCTION  *********************/
TEST(ElfReader,loadSimpleCaseGlobVars)
{
	ElfReader reader(TEST_BIN_DIR "/simple-case-no-finstr");
	ElfGlobalVariableVector vars;
	reader.loadGlobalVariables(vars);
	
	if (ElfReader::hasLibElf())
	{
		EXPECT_TRUE(hasVariable(vars,"gblArray",1024 * sizeof(int),false));
		EXPECT_TRUE(hasVariable(vars,"_ZL14gblStaticArray",1024 * sizeof(int),false));
		EXPECT_TRUE(hasVariable(vars,"_ZL9gblString",25,false));
		EXPECT_TRUE(hasVariable(vars,"tlsArray",1024 * sizeof(int),true));
	}
}

/*******************  FUNCTION  *********************/
int main(int argc, char ** argv)
{
	//init internal allocator
	gblInternaAlloc = new SimpleAllocator(true);
	
	// This allows the user to override the flag on the command line.
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}