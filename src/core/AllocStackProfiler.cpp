/*****************************************************
             PROJECT  : MATT
             VERSION  : 0.1.0-dev
             DATE     : 01/2014
             AUTHOR   : Valat Sébastien
             LICENSE  : CeCILL-C
*****************************************************/

/********************  HEADERS  *********************/
//standard
#include <cstdio>
#include <fstream>
#include <iostream>
//extension GNU
#include <execinfo.h>
//from htopml
#include <json/ConvertToJson.h>
#include <portability/OS.hpp>
//internals
#include <common/CodeTiming.hpp>
#include <common/FormattedMessage.hpp>
#include "AllocStackProfiler.hpp"

/*******************  NAMESPACE  ********************/
namespace MATT
{

/*******************  FUNCTION  *********************/
AllocStackProfiler::AllocStackProfiler(const Options & options,StackMode mode,bool threadSafe)
	:requestedMem(options.timeProfilePoints,options.timeProfileLinear)
{
	this->mode = mode;
	this->threadSafe = threadSafe;
	this->options = options;

	switch(mode)
	{
		case STACK_MODE_BACKTRACE:
			stack.loadCurrentStack();
			break;
		case STACK_MODE_ENTER_EXIT_FUNC:
			exStack.enterFunction((void*)0x1);
			exStack.exitFunction((void*)0x1);
			break;
		case STACK_MODE_USER:
			break;
	}
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onMalloc(void* ptr, size_t size,Stack * userStack)
{
	onAllocEvent(ptr,size,2,userStack);
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onCalloc(void* ptr, size_t nmemb, size_t size,Stack * userStack)
{
	onAllocEvent(ptr,size * nmemb,2,userStack);
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onFree(void* ptr,Stack * userStack)
{
	if (ptr != NULL)
		onFreeEvent(ptr,2,userStack);
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onPrepareRealloc(void* oldPtr,Stack * userStack)
{
	//nothing to unregister, skip
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onRealloc(void* oldPtr, void* ptr, size_t newSize,Stack * userStack)
{
	MATT_OPTIONAL_CRITICAL(lock,threadSafe)
		//to avoid to search it 2 times
		SimpleCallStackNode * callStackNode = NULL;
		
		//free part
		if (ptr != NULL)
			callStackNode = onFreeEvent(oldPtr,2,userStack,callStackNode,false);
		
		//alloc part
		if (newSize > 0)
			callStackNode = onAllocEvent(ptr,newSize,2,userStack,callStackNode,false);
	MATT_END_CRITICAL
}

/*******************  FUNCTION  *********************/
SimpleCallStackNode * AllocStackProfiler::onAllocEvent(void* ptr, size_t size,int skipDepth, Stack* userStack,SimpleCallStackNode * callStackNode,bool doLock)
{
	MATT_OPTIONAL_CRITICAL(lock,threadSafe && doLock)
		//update mem usage
		if (options.timeProfileEnabled)
		{
			requestedMem.onDeltaEvent(size);
			if (virtualMem.isNextPoint())
			{
				OSMemUsage mem = OS::getMemoryUsage();
				virtualMem.onUpdateValue(mem.virtualMemory);
				physicalMem.onUpdateValue(mem.physicalMemory);
			}
		}
	
		if (options.stackProfileEnabled)
		{
			//search if not provided
			if (callStackNode == NULL)
				callStackNode = getStackNode(skipDepth+1,size,userStack);
			
			//count events
			CODE_TIMING("updateInfoAlloc",callStackNode->getInfo().onAllocEvent(size));
		}

		//register for segment history tracking
		if (ptr != NULL)
			CODE_TIMING("segTracerAdd",segTracker.add(ptr,size,callStackNode));
	MATT_END_CRITICAL
	
	return callStackNode;
}

/*******************  FUNCTION  *********************/
SimpleCallStackNode * AllocStackProfiler::onFreeEvent(void* ptr,int skipDepth, Stack* userStack,SimpleCallStackNode * callStackNode,bool doLock)
{
	MATT_OPTIONAL_CRITICAL(lock,threadSafe && doLock)
		//update memory usage
		if (options.timeProfileEnabled && virtualMem.isNextPoint())
		{
			OSMemUsage mem = OS::getMemoryUsage();
			virtualMem.onUpdateValue(mem.virtualMemory);
			physicalMem.onUpdateValue(mem.physicalMemory);
		}

		//search segment info to link with previous history
		SegmentInfo * segInfo = NULL;
		if (options.timeProfileEnabled || options.stackProfileEnabled)
			CODE_TIMING("segTracerGet",segInfo = segTracker.get(ptr));
		
		//check unknown
		if (segInfo == NULL)
		{
			//fprintf(stderr,"Caution, get unknown free segment : %p, ingore it.\n",ptr);
			return NULL;
		}
			
		//update mem usage
		ssize_t size = -segInfo->size;
		if (options.timeProfileEnabled)
			requestedMem.onDeltaEvent(size);
		
		if (options.stackProfileEnabled)
		{
			//search call stack info if not provided
			if (callStackNode == NULL)
				callStackNode = getStackNode(skipDepth+1,size,userStack);
			
			//count events
			CODE_TIMING("updateInfoFree",callStackNode->getInfo().onFreeEvent(size,segInfo->getLifetime()));
		}
		
		//remove tracking info
		CODE_TIMING("segTracerRemove",segTracker.remove(ptr));
	MATT_END_CRITICAL
	
	return callStackNode;
}

/*******************  FUNCTION  *********************/
SimpleCallStackNode* AllocStackProfiler::getStackNode(int skipDepth, ssize_t delta, Stack* userStack)
{
	SimpleCallStackNode * res = NULL;

	//search with selected mode
	switch(mode)
	{
		case STACK_MODE_BACKTRACE:
			CODE_TIMING("loadCurrentStack",stack.loadCurrentStack());
			CODE_TIMING("searchInfo",res = &stackTracer.getBacktraceInfo(stack,skipDepth+1));
			break;
		case STACK_MODE_ENTER_EXIT_FUNC:
			CODE_TIMING("searchInfoEx",res = &stackTracer.getBacktraceInfo(exStack));
			break;
		case STACK_MODE_USER:
			if (userStack != NULL)
				CODE_TIMING("searchInfoUser",res = &stackTracer.getBacktraceInfo(*userStack));
			break;
	}

	return res;
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onExit(void )
{
	MATT_OPTIONAL_CRITICAL(lock,threadSafe)
		//resolve symbols
		CODE_TIMING("resolveSymbols",this->stackTracer.resolveSymbols());
	
		//open output file
		//TODO manage errors
		std::ofstream out;
		
		//config
		if (options.outputDumpConfig)
		{
			options.dumpConfig(FormattedMessage(options.outputName).arg(OS::getExeName()).arg(OS::getPID()).arg("ini").toString().c_str());
		}
		
		//lua
		if (options.outputLua)
		{
			out.open(FormattedMessage(options.outputName).arg(OS::getExeName()).arg(OS::getPID()).arg("lua").toString().c_str());
			CODE_TIMING("outputLua",htopml::convertToLua(out,*this,options.outputIndent));
			out.close();
		}

		//json
		if (options.outputJson)
		{
			out.open(FormattedMessage(options.outputName).arg(OS::getExeName()).arg(OS::getPID()).arg("json").toString().c_str());
			CODE_TIMING("outputJson",htopml::convertToJson(out,*this,options.outputIndent));
			out.close();
		}

		//valgrind out
		if (options.outputCallgrind)
		{
			ValgrindOutput vout;
			stackTracer.fillValgrindOut(vout);
			CODE_TIMING("outputCallgrind",vout.writeAsCallgrind(FormattedMessage(options.outputName).arg(OS::getExeName()).arg(OS::getPID()).arg("callgrind").toString(),stackTracer.getNameDic()));
		}
		
		//print timings
		CodeTiming::printAll();
	MATT_END_CRITICAL
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onEnterFunction ( void* funcAddr )
{
	assert(mode == STACK_MODE_ENTER_EXIT_FUNC);
	assert(!threadSafe);
	if (mode == STACK_MODE_ENTER_EXIT_FUNC)
		this->exStack.enterFunction(funcAddr);
}

/*******************  FUNCTION  *********************/
void AllocStackProfiler::onExitFunction ( void* funcAddr )
{
	assert(mode == STACK_MODE_ENTER_EXIT_FUNC);
	assert(!threadSafe);
	if (mode == STACK_MODE_ENTER_EXIT_FUNC)
		this->exStack.exitFunction(funcAddr);
}

/*******************  FUNCTION  *********************/
void convertToJson(htopml::JsonState& json, const AllocStackProfiler& value)
{
	json.openStruct();
	if (value.options.stackProfileEnabled)
		json.printField("stackInfo",value.stackTracer);
	if (value.options.timeProfileEnabled)
	{
		json.printField("requestedMem",value.requestedMem);
		json.printField("physicalMem",value.physicalMem);
		json.printField("virtualMem",value.virtualMem);
	}
	json.printField("leaks",value.segTracker);
	CODE_TIMING("ticksPerSecond",json.printField("ticksPerSecond",ticksPerSecond()));
	json.closeStruct();
}

}
