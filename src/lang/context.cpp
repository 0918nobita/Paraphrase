#include <stdarg.h>

#include "word.h"
#include "stack.h"
#include "context.h"
#include "externals.h"
#include "optimizer.h"

const int DS_InitialStackSize=4096;
const int RS_InitialStackSize=1024;
const int SS_InitialStackSize=1024;
const int IS_InitialStackSize=1024;
const int ES_InitialStackSize=1024;

static std::vector< std::vector<int> > gStackForLeave;
static std::vector< std::vector<int> > gStackForBreak;

bool Error(Context& inContext,const NoParamErrorID inErrorID);
bool Error(Context& inContext,
		   const InvalidTypeErrorID,const std::string& inStr);
bool Error(Context& inContext,
		   const InvalidValueErrorID,const std::string& inStr);
bool Error(Context& inContext,const InvalidTypeTosSecondErrorID inErrorID,
		   const TypedValue& inTos,const TypedValue& inSecond);
bool Error(Context& inContext,const InvalidTypeStrTvTvErrorID inErrorID,
		   const std::string& inStr,const TypedValue inTV1,const TypedValue& inTV2);
bool Error(Context& inContext,const ErrorIdWithInt inErrorID,
		   const int inIntValue);
bool Error(Context& inContext,const ErrorIdWithString inErrorID,
		   const char *inStr);
bool Error(Context& inContext,
		   const ErrorIdWithString inErrorID,const char *inStr);
bool Error(Context& inContext,const ErrorIdWith2int inErrorID,
		   const int inIntValue1,const int inIntValue2);

PP_API Context *GlobalContext=new Context(NULL,Level::kInterpret,NULL,NULL,NULL);

PP_API Context::Context(Context *inParent,Level inExecutionThreshold,
				 std::shared_ptr<ChanMan> inFromParent,
				 std::shared_ptr<ChanMan> inToParent,
				 const std::vector<TypedValue> *inInitParamFromParent)
  :parent(inParent),ExecutionThreshold(inExecutionThreshold),
   fromParent(inFromParent),toParent(inToParent),
   toChild(),fromChild(),toPipe(),
   fromPipe(inParent!=NULL ? inParent->lastOutputPipe : NULL),
   lastOutputPipe(),initParamForPBlock(),isInitParamBroadcast(true) {
	DS.reserve(DS_InitialStackSize);
	RS.reserve(RS_InitialStackSize);
	SS.reserve(SS_InitialStackSize);
	IS.reserve(IS_InitialStackSize);

	newWord=NULL;

	if(inInitParamFromParent!=NULL) {
		const size_t n=inInitParamFromParent->size();
		for(size_t i=0; i<n; i++) {
			DS.push_back(inInitParamFromParent->at(i));
		}
	}
}


PP_API void Context::RemoveDefiningWord() {
	if(newWord->shortName.length()!=0) {
		Uninstall(newWord);
	}
	delete newWord;
	newWord=NULL;
	lastDefinedWord=NULL;
}

PP_API bool Context::Exec(const TypedValue& inTV) {
	if(inTV.HasWordPtr()==false) {
		return Error(InvalidTypeErrorID::E_TOS_WP,inTV);
	}
	return Exec(inTV.wordPtr);
}

PP_API bool Context::Exec(const std::string inWordName) {
	auto iter=Dict.find(inWordName);
	if(iter==Dict.end()) {
		return Error(ErrorIdWithString::E_CAN_NOT_FIND_THE_WORD,inWordName);
	}
	return Exec(iter->second);
}

PP_API bool Context::Exec(const Word *inWord) {
	const Word **ipBackup=ip;
		const Word *tmpThread[2]={inWord,NULL};
		ip=tmpThread;
		bool needLVEnv = inWord->numOfLocalVar>0;
		if(needLVEnv) {
			Env.push_back(LocalVarSlot(inWord->numOfLocalVar,TypedValue()));
		}
		bool ret=innerInterpreter(*this);
		if(ret==false) { IS.clear(); }
		if( needLVEnv ) { Env.pop_back(); }
	ip=ipBackup;
	return ret;
}

PP_API bool Context::Compile(const std::string& inWordName) {
	const Word *wordPtr=GetWordPtr(inWordName);
	if(wordPtr==NULL) { return false; }
	Compile(TypedValue(wordPtr));
	return true;
}

PP_API void Context::Compile(const TypedValue& inTypedValue) {
	newWord->tmpParam->push_back(inTypedValue);
}

PP_API bool Context::Compile(int inAddress,
							 const TypedValue& inTypedValue,
							 bool inIsForceUpdate) {
	if(newWord->tmpParam->size()<=inAddress) {
		fprintf(stderr,"SYSTEM ERROR at Compile(addr,tv).\n");
		return false;
	}
	TypedValue& emptySlot=newWord->tmpParam->at(inAddress);
	if(inIsForceUpdate==false && emptySlot.dataType!=DataType::kTypeEmptySlot) {
		fprintf(stderr,"SYSTEM ERROR at Compile(addr,tv).\n");
		fprintf(stderr,"slot is not an empty slot.\n");
		fprintf(stderr,"current slot is:");
		emptySlot.Dump();
		return false;
	}
	newWord->tmpParam->at(inAddress)=inTypedValue;
	return true;
}

PP_API bool Context::BeginControlBlock() {
	RS.emplace_back(ExecutionThreshold);
	if(ExecutionThreshold==Level::kInterpret) {
		if(newWord!=NULL) { return false; }
		newWord=new Word(G_DocolAdvice);
		SetCompileMode();
	}
	return true;
}

PP_API bool Context::EndControlBlock() {
	if(RS.size()<1) { return Error(NoParamErrorID::E_RS_BROKEN); }
	TypedValue tvThreshold=Pop(RS);
	if(tvThreshold.dataType!=DataType::kTypeThreshold) {
		return Error(InvalidTypeErrorID::E_RS_TOS_THRESHOLD,tvThreshold);
	}
	if(tvThreshold.intValue==(int)Level::kInterpret) {
		Compile(std::string("_semis"));
		Word *newWordBackup=newWord;
		FinishNewWord();
		TypedValue elementToExec(newWordBackup);
		newWord=NULL;
		ExecutionThreshold=(Level)tvThreshold.intValue;
		return Exec(elementToExec);
	} else {
		return true;
	}
}

PP_API void Context::BeginNoNameWordBlock() {
	RS.emplace_back(DataType::kTypeNewWord,newWord);
	newWord=new Word(G_DocolAdvice);
	RS.emplace_back(ExecutionThreshold);
	SetCompileMode();
}

PP_API bool Context::EndNoNameWordBlock() {
	if(RS.size()<2) { return Error(NoParamErrorID::E_RS_AT_LEAST_2); }
	TypedValue tvThreshold=Pop(RS);
	if(tvThreshold.dataType!=DataType::kTypeThreshold) {
		return Error(InvalidTypeErrorID::E_RS_TOS_THRESHOLD,tvThreshold);
	}
	TypedValue newWordElement=Pop(RS);
	if(newWordElement.dataType!=DataType::kTypeNewWord) {
		return Error(InvalidTypeErrorID::E_RS_SECOND_WP,newWordElement);
	}
	
	newWord=(Word *)newWordElement.wordPtr;
	ExecutionThreshold=(Level)tvThreshold.intValue;
	if(ExecutionThreshold==Level::kInterpret && newWord!=NULL) {
		Error(NoParamErrorID::E_SYSTEM_ERROR);
		exit(-1);
	}
	return true;
}

PP_API void Context::BeginListBlock() {
	RS.emplace_back(DataType::kTypeNewWord,newWord);
	newWord=new Word(WordType::List);
	RS.emplace_back(ExecutionThreshold);
	SetSymbolMode();
}

PP_API bool Context::EndListBlock() {
	if(RS.size()<2) { return Error(NoParamErrorID::E_RS_AT_LEAST_2); }
	TypedValue tvThreshold=Pop(RS);
	if(tvThreshold.dataType!=DataType::kTypeThreshold) {
		return Error(InvalidTypeErrorID::E_RS_TOS_THRESHOLD,tvThreshold);
	}

	TypedValue newWordElement=Pop(RS);
	if(newWordElement.dataType!=DataType::kTypeNewWord) {
		return Error(InvalidTypeErrorID::E_RS_SECOND_WP,newWordElement);
	}

	newWord=(Word *)newWordElement.wordPtr;
	ExecutionThreshold=(Level)tvThreshold.intValue;
	return true;
}

int Context::ReadThresholdBackup() {
	if(RS.size()<1) {
		Error(NoParamErrorID::E_RS_IS_EMPTY);
		return -1;
	}
	TypedValue& tvThreshold=ReadTOS(RS);
	if(tvThreshold.dataType!=DataType::kTypeThreshold) {
		Error(InvalidTypeErrorID::E_RS_TOS_THRESHOLD,tvThreshold);
		return -1;
	}
	return tvThreshold.intValue;
}

PP_API TypedValue Context::MarkHere(int inOffset) {
	return TypedValue(DataType::kTypeAddress,GetNextThreadAddressOnCompile()+inOffset);
}

PP_API int Context::GetChunkIndex(const int inCbtMask) {
	if(SS.size()<2) {
		Error(NoParamErrorID::E_SS_BROKEN);
		return -1;
	}
	// chunk size contains CB info.
	// ex:
	//     +------------------+
	//     | kSyntax_IF       |
	//     +------------------+
	//     | 3 (==chunk size) |
	//     +------------------+
	//     | alpha            |
	//     +------------------+
	int chunkSize=0;
	for(int i=(int)SS.size()-1; i>0; i-=chunkSize) {
		const TypedValue& tvCB=SS.at(i);
		if(tvCB.dataType!=DataType::kTypeCB) {
			Error(NoParamErrorID::E_SS_BROKEN);
			return -1;
		}
		if((tvCB.intValue & inCbtMask)!=0) {
			return i;
		}
		if(i-1<0) {
			Error(NoParamErrorID::E_SS_BROKEN);
			return -1;
		}
		const TypedValue& tvChunkSize=SS.at(i-1);
		if(tvChunkSize.dataType!=DataType::kTypeInt) {
			Error(NoParamErrorID::E_SS_BROKEN);
			return -1;
		}
		if(tvChunkSize.intValue<=1) {
			Error(NoParamErrorID::E_SS_BROKEN);
			return -1;
		}
		chunkSize=tvChunkSize.intValue;
	}
	return -1;
}

PP_API bool Context::RemoveTopChunk() {
	TypedValue& tvCB=ReadTOS(SS);
	if(tvCB.dataType!=DataType::kTypeCB) { return Error(NoParamErrorID::E_SS_BROKEN); }
	TypedValue& tvChunkSize=ReadSecond(SS);
	if(tvChunkSize.dataType!=DataType::kTypeInt) {
		return Error(NoParamErrorID::E_SS_BROKEN);
	}
	int chunkSize=tvChunkSize.intValue;
	if(chunkSize<0) { return Error(NoParamErrorID::E_SS_BROKEN); }
	if(SS.size()<chunkSize) { return Error(NoParamErrorID::E_SS_BROKEN); }
	for(int i=0; i<chunkSize; i++) {
		SS.pop_back();
	}
	return true;
}

PP_API void Context::CompileEmptySlot(const Level inThresholdBackup) {
	TypedValue tv=TypedValue(DataType::kTypeEmptySlot,inThresholdBackup);
	Compile(tv);
}

PP_API TypedValue Context::MarkAndCreateEmptySlot() {
	TypedValue ret=MarkHere();
	CompileEmptySlot(ExecutionThreshold);
	return ret;
}

PP_API int Context::GetNextThreadAddressOnCompile() const {
	return (int)(newWord->tmpParam->size());
}


PP_API TypedValue Context::GetList() const {
	assert(newWord->type==WordType::List);
	TypedValue tvList(new List());
	size_t n=newWord->tmpParam->size();
	for(size_t i=0; i<n; i++) {
		tvList.listPtr->push_back(newWord->tmpParam->at(i));
	}
	return tvList;
}

const char *Context::GetExecutingWordName() const {
	if(ip==NULL) { return ""; }
	const char *wordName=(*ip)->longName.c_str();
	return wordName;
}

PP_API bool Context::Error(const NoParamErrorID inErrorID) {
	return ::Error(*this,inErrorID);
}

PP_API bool Context::Error(const ErrorIdWithInt inErrorID,
						   const int inIntValue) {
	return ::Error(*this,inErrorID,inIntValue);
}

PP_API bool Context::Error(const ErrorIdWith2int inErrorID,
						   const int inIntValue1,const int inIntValue2) {
	return ::Error(*this,inErrorID,inIntValue1,inIntValue2);
}

PP_API bool Context::Error(const ErrorIdWithString inErrorID,
						   const std::string& inStr) {
	return ::Error(*this,inErrorID,inStr.c_str());
}

PP_API bool Context::Error(const InvalidTypeErrorID inErrorID,
						   const TypedValue& inTV) {
	return ::Error(*this,inErrorID,inTV.GetTypeStr());
}

PP_API bool Context::Error(const InvalidValueErrorID inErrorID,
						   const TypedValue& inTV) {
	return ::Error(*this,inErrorID,inTV.GetValueString());
}

PP_API bool Context::Error(const InvalidTypeTosSecondErrorID inErrorID,
						   const TypedValue& inTos,const TypedValue& inSecond) {
	return ::Error(*this,inErrorID,inTos,inSecond);
}

PP_API bool Context::Error_InvalidType(const InvalidTypeStrTvTvErrorID inErrorID,
								const std::string& inStr,
								const TypedValue inTV1,const TypedValue& inTV2) {
	return ::Error(*this,inErrorID,inStr,inTV1,inTV2);
}

// tmpParam -> param
static int getParamSize(const std::vector<TypedValue> *inTmpParam);

PP_API void Context::FinishNewWord() {
	Optimize(newWord->tmpParam);
	ReplaceTailRecursionToJump(newWord,newWord->tmpParam);

	int paramSize=getParamSize(newWord->tmpParam);

	const size_t n=newWord->tmpParam->size();
	int *addressTransferMap=new int[paramSize];
	size_t newIndex=0;
	for(int i=0; i<n; i++) {
		addressTransferMap[i]=(int)newIndex;
		TypedValue tv=newWord->tmpParam->at(i);
		switch(tv.dataType) {
			case DataType::kTypeAddress:
			case DataType::kTypeDirectWord: newIndex++; break;

			default:
				newIndex += sizeof(TypedValue)%sizeof(WordFunc*)==0
					 	  ? sizeof(TypedValue)/sizeof(WordFunc*)
					 	  : sizeof(TypedValue)/sizeof(WordFunc*)+1;
		}
	}

	const Word **wordPtrArray=new const Word*[paramSize+1];
	std::unordered_map<int,int> *addressOffsetToIndexMapper
		=new std::unordered_map<int,int>();
	int dest=0;
	for(int i=0; i<n; i++) {
		addressOffsetToIndexMapper->insert({dest,i});
		TypedValue& tv=newWord->tmpParam->at(i);
		if(tv.dataType==DataType::kTypeDirectWord) {
			wordPtrArray[dest]=tv.wordPtr;
			dest++;
		} else if(tv.dataType==DataType::kTypeAddress) {
			wordPtrArray[dest]
				=(const Word*)(wordPtrArray+addressTransferMap[tv.intValue]);
			dest++;
		} else {
			new((TypedValue*)(wordPtrArray+dest)) TypedValue(tv);
			dest += TvSize;
 		}
	}
	wordPtrArray[paramSize]=NULL;	// guard (end mark)

	newWord->param=wordPtrArray;
	newWord->addressOffsetToIndexMapper=addressOffsetToIndexMapper;
	newWord->isForgetable=true;
	lastDefinedWord=newWord;
	newWord=NULL;
}

PP_API void Context::ShowIS() const {
	int maxLength=-1;
	for(int i=0; i<(int)IS.size(); i++) {
		const Word *word=*(IS[i]);
		maxLength=std::max(maxLength,(int)(word->longName.size()));
	}

	printf(" +");
	for(int i=0; i<maxLength; i++) { putchar('-'); }
	printf("+ \n");

	for(int i=(int)IS.size()-1; i>=0; i--) {
		printf(" |");
		const Word *word=*IS[i];
		const int len=(int)(word->longName.size());
		int gap=(maxLength-len)/2;
		for(int j=0; j<gap; j++) { putchar(' '); }
		printf("%s",word->longName.c_str());
		for(int j=0; j<maxLength-gap-len; j++) { putchar(' '); }
		printf("|\n");
	}

	for(int i=0; i<maxLength+2*2; i++) { putchar('-'); }
	printf("\n");
}

static int getParamSize(const std::vector<TypedValue> *inTmpParam) {
	const size_t n=inTmpParam->size();
	size_t ret=0;
	for(size_t i=0; i<n; i++) {
		TypedValue tv=inTmpParam->at(i);
		switch(tv.dataType) {
			case DataType::kTypeAddress:
			case DataType::kTypeDirectWord:	ret++;	break;
			default: ret += TvSize;
		}
	}
	return (int)ret;
}

PP_API bool Context::IsInComment() {
	if(RS.size()>0) {
		TypedValue& rsTos=ReadTOS(RS);
		return rsTos.dataType==DataType::kTypeMiscInt
		  && (rsTos.intValue==(int)ControlBlockType::kOPEN_C_STYLE_COMMENT
			  || rsTos.intValue==(int)ControlBlockType::kOPEN_CPP_STYLE_ONE_LINE_COMMENT);
	}
	return false;
}

PP_API bool Context::IsInCStyleComment() {
	if(RS.size()>0) {
		TypedValue& rsTos=ReadTOS(RS);
		if(rsTos.dataType==DataType::kTypeMiscInt
		   && rsTos.intValue==(int)ControlBlockType::kOPEN_C_STYLE_COMMENT) {
			return true;
		}
	}
	return false;
}

PP_API bool Context::IsInCppStyleComment() {
	if(RS.size()>0) {
		TypedValue& rsTos=ReadTOS(RS);
		if(rsTos.dataType==DataType::kTypeMiscInt
		  && rsTos.intValue==(int)ControlBlockType::kOPEN_CPP_STYLE_ONE_LINE_COMMENT) {
			return true;
		}
	}
	return false;
}

PP_API void Context::EnterLevel2(const ControlBlockType inCB_ID) {
	PushThreshold();
	PushNewWord();
	RS.emplace_back(DataType::kTypeMiscInt,inCB_ID);
	newWord=new Word(WordType::Normal);
	ExecutionThreshold=Level::kSymbol;
}

PP_API bool Context::LeaveLevel2() {
	delete newWord->tmpParam;
	delete newWord;

	if(DS.size()<2) { return Error(NoParamErrorID::E_DS_AT_LEAST_2); }
	TypedValue tvNW=Pop(DS);
	if(tvNW.dataType!=DataType::kTypeNewWord) {
		return Error(InvalidTypeErrorID::E_TOS_NEW_WORD,tvNW);
	}
 	newWord=(Word *)tvNW.wordPtr;

	TypedValue tvThreshold=Pop(DS);
	if(tvThreshold.dataType!=DataType::kTypeThreshold) {
		return Error(InvalidTypeErrorID::E_SECOND_THRESHOLD,tvThreshold);
	}
	ExecutionThreshold=(Level)tvThreshold.intValue;
	return true;
}

PP_API bool Context::EnterHereDocument(ControlBlockType inCB_ID) {
	if( IsInCStyleComment() )   { return false; }
	if( IsInCppStyleComment() ) { return false; }
	if(ExecutionThreshold==Level::kSymbol) { return false; }
	EnterLevel2(inCB_ID);
	return true;
}

PP_API bool Context::LeaveHereDocument() {
	if(LeaveLevel2()==false) { return false; }
	switch(ExecutionThreshold) {
		case Level::kInterpret:
			DS.emplace_back(hereDocStr);
			break;
		case Level::kCompile:
			Compile(std::string("_lit"));
			Compile(TypedValue(hereDocStr));
			break;
		default:
			fprintf(stderr,"SYSTEM ERROR, invalid execution threshold (in >>>raw)\n");
			exit(-1);
	}
	hereDocStr="";
	return true;
}

void Error(const char *inWordName,const char *inFormat,...) {
	if(inWordName!=NULL) {
		fprintf(stderr,"ERROR '%s': ",inWordName);
	}
	va_list vl;
	va_start(vl,inFormat);
	vfprintf(stderr,inFormat,vl);
	va_end(vl);
}

void Error(const Context& inContext,const char *inWordName,const char *inFormat,...) {
	if( inContext.suppressError ) {
		return;
	}
	if(inWordName!=NULL) {
		fprintf(stderr,"ERROR '%s': ",inWordName);
	}
	va_list vl;
	va_start(vl,inFormat);
	vfprintf(stderr,inFormat,vl);
	va_end(vl);
}

