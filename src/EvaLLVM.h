//
// Created by Valerian on 2024/2/22.
//

#ifndef EVA_EVALLVM_H
#define EVA_EVALLVM_H
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "parser/EvaParser.h"
#include "Enviroment.h"
#include "Logger.h"
#include <regex>
#include <memory>
#include <map>

//ClasInfo
struct ClassInfo{
    llvm::StructType* cls;
    llvm::StructType* parent;
    std::map<std::string,llvm::Type*> fieldsMap;
    std::map<std::string,llvm::Function*> methodsMap;
};

using Env = std::shared_ptr<Environment>;
class EvaLLVM{
public:
    EvaLLVM() : parser(std::make_unique<EvaParser>()) {
        moduleInit();
        setupExternFunction();
        setupGlobalEnvironment();
        setupTargetTriple();
    }

    void exec(const std::string& program){
        //1.parse the program
        auto ast = parser->parse("(begin" + program + ")");

        //2.compile to llvm ir
        compile(ast);

        //the struct type if not used will be optimized
        auto *gv = new llvm::GlobalVariable(*module,classMap["Point"].cls, false,llvm::GlobalValue::ExternalLinkage, nullptr, "GlobalVar");

        module->print(llvm::outs(),nullptr, false,true);

        //3. save module IR to file
        saveModuleToFile("./out.ll");
    }
private:
    void compile(const Exp& ast){
        //
        fn = createFunction("main",
                            llvm::FunctionType::get(builder->getInt32Ty(),false),GlobalEnv);

        gen(ast,GlobalEnv);

        //hard code way to return 0
        builder->CreateRet(builder->getInt32(0));
    }

    void setupGlobalEnvironment(){
        std::map<std::string, llvm::Value*> globalObject{
                {"VERSION",builder->getInt32(42)},
        };

        std::map<std::string,llvm::Value*> globalRec{};
        for(auto& entry : globalObject){
            globalRec[entry.first] = createGlobalVariable(entry.first,llvm::cast<llvm::Constant>(entry.second));
        }

        GlobalEnv = std::make_shared<Environment>(globalRec,nullptr);
    }

    void setupExternFunction(){
        auto charPtrTy = builder->getInt8Ty()->getPointerTo();
        module->getOrInsertFunction("printf",llvm::FunctionType::get(builder->getInt32Ty(),charPtrTy,true));


        //void *malloc(size_t size)
        module->getOrInsertFunction("GC_malloc",llvm::FunctionType::get(charPtrTy,builder->getInt64Ty(),false));
    }

    void setupTargetTriple(){
        module->setTargetTriple("arm64-apple-macosx12.0.0");
    }

    llvm::Function* createFunctionProto(const std::string& fnName,
                                        llvm::FunctionType* fnType,
                                        const Env& env){
        auto function = llvm::Function::Create(fnType,llvm::Function::ExternalLinkage,fnName,*module);

        //install in the global environment
        env->define(fnName,function);

        llvm::verifyFunction(*function);

        return function;
    }

    //(x number)
    // x
    std::string extractVarName(const Exp& exp){
        return exp.type == ExpType::LIST ? exp.list[0].string : exp.string;
    }

    llvm::Type* getTypeFromString(const std::string& type){
        if(type == "number"){
            return builder->getInt32Ty();
        }
        if(type == "string"){
            return builder->getInt8Ty()->getPointerTo();
        }

        return classMap[type].cls->getPointerTo();
    }

    //(x number)
    // TODO better to define type according to the expression
    llvm::Type* extractVarType(const Exp& exp){
        return exp.type == ExpType::LIST ? getTypeFromString(exp.list[1].string) : builder->getInt32Ty();
    }


    llvm::Value* allocVar(const std::string& name, llvm::Type* type, const Env& env){
        varsBuilder->SetInsertPoint(&fn->getEntryBlock());

        auto varAlloc = varsBuilder->CreateAlloca(type,0,nullptr);

        env->define(name,varAlloc);

        return varAlloc;
    }

    bool hasReturnType(const Exp& fnExp){
        return fnExp.list[3].type == ExpType::SYMBOL && fnExp.list[3].string == "->";
    }

    llvm::FunctionType* extractFunctinoType(const Exp& fnExp){
        auto params = fnExp.list[2];

        auto returnType = hasReturnType(fnExp)
                                ? getTypeFromString(fnExp.list[4].string)
                                : builder->getInt32Ty();


        std::vector<llvm::Type*> paramTypes{};
        for(auto& param : params.list){
            auto paramName = extractVarName(param);
            auto paramTy = extractVarType(param);
            paramTypes.push_back(
                    paramName == "self" ? (llvm::Type*)cls->getPointerTo() : paramTy);
        }
        return llvm::FunctionType::get(returnType,paramTypes,false);
    }

    //Untyped: (def square (x) (* x x))
    //Typed: (def square ((x number)) -> number (* x x))
    llvm::Value* compileFunction(const Exp& fnExp, std::string fnName,Env& env){
        //
        auto params = fnExp.list[2];
        auto body = hasReturnType(fnExp) ? fnExp.list[5] : fnExp.list[3];

        //override fn to compile body
        auto prevFn = fn;
        auto prevBlock = builder->GetInsertBlock();

        auto originName = fnName;
        if(cls != nullptr){
            fnName = std::string(cls->getName().data()) + "_" + fnName;
        }
        auto newFn = createFunction(fnName, extractFunctinoType(fnExp),env);
        fn = newFn;

        //Set parameter names;
        auto idx = 0;

        auto fnEnv = std::make_shared<Environment>(
                std::map<std::string,llvm::Value*>{},env);

        for(auto& arg : fn->args()){
            auto param = params.list[idx++];
            auto argName = extractVarName(param);

            auto argBinding = allocVar(argName,arg.getType(),fnEnv);
            builder->CreateStore(&arg,argBinding);
        }

        builder->CreateRet(gen(body,fnEnv));

        //restore previous fn after compiling
        builder->SetInsertPoint(prevBlock);
        fn = prevFn;

        // wired
        return newFn;
    }

    size_t getTypeSize(llvm::StructType* type){
        return module->getDataLayout().getTypeAllocSize(type);
    }

    //Allocates an object of a given class on the heap
    llvm::Value* mallocInstance(llvm::StructType* _cls,const std::string& name){
        auto typeSize = builder->getInt64(getTypeSize(_cls));
        auto mallocPtr = builder->CreateCall(module->getFunction("GC_malloc"),typeSize,name);
        auto instance = builder->CreatePointerCast(mallocPtr,_cls->getPointerTo());

        //Install the vTable to lookup methods
        std::string className = _cls->getName().data();
        auto vTableName = className + "_vTable";
        auto vTableAddr = builder->CreateStructGEP(_cls,instance,VTABLE_INDEX);
        auto vTable = module->getNamedGlobal(vTableName);
        builder->CreateStore(vTable,vTableAddr);

        return instance;
    }

    llvm::Value* createInstance(const Exp& exp, Env& env, const std::string& name){
        //new Instance [arguments]
        auto className = exp.list[1].string;
        auto _cls = getClassByName(className);

        if(_cls == nullptr){
            DIE << "[EvaLLVM]: Unknown class" << className;
        }

//        //NOTE: Stack allocation: (TODO: heap allocation)
//        auto instance = name.empty() ? builder->CreateAlloca(_cls)
//                                     : builder->CreateAlloca(_cls,0,name);

//      llvm has built-in instruction          llvm::CallInst::CreateMalloc()

        auto instance = mallocInstance(_cls,name);
        //We do not use stack allocation for objects, since we need to support constructor (factory) pattern
        //
        //Call constructor:
        auto ctor = module->getFunction(className + "_constructor");

        std::vector<llvm::Value*> args{instance};

        for(auto i = 2; i < exp.list.size(); ++i){
            args.push_back(gen(exp.list[i],env));
        }

        builder->CreateCall(ctor,args);

        return instance;

    }


#define GEN_BINARY_OP(Op,varName) \
    do{                           \
        auto op1 = gen(exp.list[1],env); \
        auto op2 = gen(exp.list[2],env); \
        return builder->Op(op1,op2,varName);                    \
    }while(false);



    llvm::Value* gen(const Exp& exp,Env& env){
        switch(exp.type) {
            case ExpType::NUMBER:
                return builder->getInt32(exp.number);

            case ExpType::STRING: {
                auto re = std::regex("\\\\n");
                auto str = std::regex_replace(exp.string, re, "\n");
                return builder->CreateGlobalStringPtr(str);
            }

            case ExpType::SYMBOL: {
                //Boolean
                if (exp.string == "true" || exp.string == "false") {
                    return builder->getInt1(exp.string == "true");
                } else {
                    //Variable
                    auto varName = exp.string;

                    auto value = env->lookup(varName);
                    //1 local (TODO)
                    if (auto localVar = llvm::dyn_cast<llvm::AllocaInst>(value)) {

                        return builder->CreateLoad(localVar->getAllocatedType(), localVar);
                    }

                    //2 global
                    if (auto globalVar = llvm::dyn_cast<llvm::GlobalVariable>(value)) {
                        return builder->CreateLoad(globalVar->getInitializer()->getType(), globalVar);
                    }

                    //function
                    return value;
                }
            }

            case ExpType::LIST:
                auto tag = exp.list[0];

                /* special cases */
                if (tag.type == ExpType::SYMBOL) {
                    auto op = tag.string;
                    //printf extern function
                    //(printf "Value: %d" 42)
                    if (op == "printf") {
                        auto printfFn = module->getFunction("printf");

                        std::vector<llvm::Value *> args{};

                        //handle args start from 1
                        for (unsigned i = 1; i < exp.list.size(); ++i) {
                            auto value = gen(exp.list[i], env);
                            args.push_back(value);
                        }

                        return builder->CreateCall(printfFn, args);
                    } else if (op == "var") {
                        if(cls != nullptr){
                            return builder->getInt32(0);
                        }

                        //normal (var x (+ y 10))

                        //Typed: (var (x number) 42)

                        //Note locals are allocated on the stack that needed to store into stack
                        auto varNameDecl = exp.list[1];
                        auto varName = extractVarName(varNameDecl);

                        //Special case for new as it allocates a variable
                        //var p (new Point a b)

                        if(isNew(exp.list[2])){
                            auto instance = createInstance(exp.list[2],env,varName);
                            return env->define(varName,instance);
                        }

                        auto init = gen(exp.list[2], env);

                        //Type
                        auto varTy = extractVarType(varNameDecl);

                        //Variable
                        auto varBinding = allocVar(varName, varTy, env);


                        return builder->CreateStore(init, varBinding);
                    } else if (op == "begin") {
                        //Compile each expression within the block
                        //Result is the last evaluated expression
                        auto blockEnv = std::make_shared<Environment>(std::map<std::string, llvm::Value *>{}, env);
                        llvm::Value *blockRes;
                        for (auto i = 1; i < exp.list.size(); ++i) {
                            //Generate expression code
                            blockRes = gen(exp.list[i], blockEnv);// TODO
                        }
                        return blockRes;
                    } else if (op == "set") {
                        //new Value
                        auto newValue = gen(exp.list[2], env);

                        //1. Properties
                        //(set (prop p x) 1)
                        if(isProp(exp.list[1])){
                            auto instancePtr = gen(exp.list[1].list[1],env);
                            auto fieldName = exp.list[1].list[2].string;
                            auto ptrName = std::string("p") + fieldName;

                            auto _cls = llvm::dyn_cast<llvm::StructType>(instancePtr->getType()->getContainedType(0));
                            auto fieldIdx = getFieldIndex(_cls,fieldName);

                            auto address = builder->CreateStructGEP(_cls,instancePtr,fieldIdx);

                            builder->CreateStore(newValue,address);

                            return newValue;
                        }else{
                            //2. Variables
                            std::string name = exp.list[1].string;
                            llvm::Value *setVar = env->lookup(name);

                            if (setVar == nullptr) {
                                DIE << "Undefined Var " << name << std::endl;
                            }

                            //create store
                            builder->CreateStore(newValue, setVar);

                            return newValue;
                        }
                    } else if (op == "+") {
                        //(+ x y)
                        GEN_BINARY_OP(CreateAdd, "")
                    } else if (op == "-") {
                        GEN_BINARY_OP(CreateSub, "")
                    } else if (op == "*") {
                        GEN_BINARY_OP(CreateSub, "")
                    } else if (op == "/") {
                        GEN_BINARY_OP(CreateSDiv, "")
                    } else if (op == ">") {
                        GEN_BINARY_OP(CreateICmpSGT, "")
                    } else if (op == "<") {
                        GEN_BINARY_OP(CreateICmpSLT, "")
                    } else if (op == "==") {
                        GEN_BINARY_OP(CreateICmpEQ, "")
                    } else if (op == "!=") {
                        GEN_BINARY_OP(CreateICmpNE, "")
                    } else if (op == "if") {
                        //if <cond> <then> <else>:

                        auto cond = gen(exp.list[1], env);
                        auto thenBlock = createBB("then", fn);
                        auto elseBlock = createBB("else");
                        auto ifEndBlock = createBB("ifend");
                        builder->CreateCondBr(cond, thenBlock, elseBlock);

                        //Then branch
                        builder->SetInsertPoint(thenBlock);
                        auto thenRes = gen(exp.list[2], env);
                        builder->CreateBr(ifEndBlock);

                        thenBlock = builder->GetInsertBlock();


                        //else branch
                        fn->getBasicBlockList().push_back(elseBlock);
//                        fn->insert(fn->end(), elseBlock); llvm@17
                        builder->SetInsertPoint(elseBlock);
                        auto elseRes = gen(exp.list[3], env);
                        builder->CreateBr(ifEndBlock);
                        //restore else block
                        elseBlock = builder->GetInsertBlock();

                        fn->getBasicBlockList().push_back(ifEndBlock);
//                        fn->insert(fn->end(), ifEndBlock);
                        builder->SetInsertPoint(ifEndBlock);

                        //I don't think this is appropriate
//                        auto phi = builder->CreatePHI(thenRes->getType(),2);
//                        phi->addIncoming(thenRes,thenBlock);
//                        phi->addIncoming(elseRes,elseBlock);

                        return nullptr;
                    } else if (op == "while") {

                        // while cond body
                        //condition:
                        auto condBlock = createBB("", fn);
                        builder->CreateBr(condBlock);


                        //
                        auto bodyBlock = createBB("body");
                        auto loopEndBlock = createBB("loopend");

                        //compile <cond>
                        builder->SetInsertPoint(condBlock);
                        auto cond = gen(exp.list[1], env);
                        builder->CreateCondBr(cond, bodyBlock, loopEndBlock);

                        fn->getBasicBlockList().push_back(bodyBlock);
//                        fn->insert(fn->end(), bodyBlock);
                        builder->SetInsertPoint(bodyBlock);
                        gen(exp.list[2], env);
                        builder->CreateBr(condBlock);

                        fn->getBasicBlockList().push_back(loopEndBlock);
//                        fn->insert(fn->end(), loopEndBlock);
                        builder->SetInsertPoint(loopEndBlock);

                        return builder->getInt32(0);
                    } else if (op == "def") {
                        //Function declaration: (def <name> <params> <body>)
                        return compileFunction(exp, exp.list[1].string, env);
                    } else if (op == "class"){
                        //class
                        // (class A <super class> <body>)
                        auto name = exp.list[1].string;

                        auto parent = exp.list[2].string == "null"
                                        ? nullptr
                                        : getClassByName(exp.list[2].string);

                        cls = llvm::StructType::create(*ctx,name);

                        if(parent != nullptr){
                            inheritClass(cls,parent);
                        }else{
                            classMap[name] = {cls,parent,{},{}};
                        }

                        //You need to know about class methods & fields info before going to the body
                        buildClassInfo(cls,exp,env);

                        gen(exp.list[3],env);

                        cls = nullptr;

                        return builder->getInt32(0);

                    }else if(op == "new"){
                        return createInstance(exp,env,"");
                    }else if(op == "prop"){
                        //(prop self x)
                        //(prop p x)
                        auto instancePtr = gen(exp.list[1],env);

                        auto fieldName = exp.list[2].string;
                        auto ptrName = std::string("p") + fieldName;

                        auto cls = (llvm::StructType *)(instancePtr->getType()->getContainedType(0));
                        auto fieldIdx = getFieldIndex(cls, fieldName);
                        auto address = builder->CreateStructGEP(cls, instancePtr, fieldIdx);

                        return builder->CreateLoad(cls->getElementType(fieldIdx), address);
                    }else if(op == "method"){
                        //(method <instance> <name>)
                        //(method (super <class>) <name>)
                        auto methodName = exp.list[2].string;

                        llvm::StructType *cls;
                        llvm::Value *vTable;
                        llvm::StructType *vTableTy;

                        if (isSuper(exp.list[1])){
                            auto className = exp.list[1].list[1].string;
                            cls = classMap[className].parent;
                            auto parentName = std::string{cls->getName().data()};
                            //pointer type
                            vTable = module->getNamedGlobal(parentName + "_vTable");
                            vTableTy = llvm::StructType::getTypeByName(*ctx, parentName + "_vTable");
                            assert(llvm::isa<llvm::PointerType>(vTable->getType()));
                        }else{
                            auto instance = gen(exp.list[1], env);
                            cls = llvm::dyn_cast<llvm::StructType>(instance->getType()->getContainedType(0));
                            auto vTableAddr = builder->CreateStructGEP(cls, instance, VTABLE_INDEX);
                            vTable = builder->CreateLoad(cls->getElementType(VTABLE_INDEX), vTableAddr, "vt");
                            vTableTy = llvm::dyn_cast<llvm::StructType>(vTable->getType()->getContainedType(0));
                        }

                        auto methodIdx = getMethodIndex(cls, methodName);
                        //Function Pointer type
                        auto methodTy = llvm::dyn_cast<llvm::PointerType>(vTableTy->getElementType(methodIdx));
                        auto methodAddr = builder->CreateStructGEP(vTableTy, vTable, methodIdx);
                        return builder->CreateLoad(methodTy, methodAddr);
                    }else{
                        //Function calls
                        //(square 2)
                        auto callable = gen(exp.list[0],env);

                        auto fn = llvm::dyn_cast<llvm::Function>(callable);

                        std::vector<llvm::Value*> args{};

                        auto argIdx = 0;
                        //llvm::Value*
                        for(auto i = 1; i < exp.list.size(); ++i){
                            auto argValue = gen(exp.list[i],env);
                            auto paramType = fn->getArg(argIdx++)->getType();
                            auto bitCastArg = builder->CreateBitCast(argValue,paramType);
                            args.push_back(bitCastArg);
                        }

                        auto calledFn = (llvm::Function*) callable;

                        return builder->CreateCall(calledFn,args);
                    }
                }else{
                    //((method p getX) p 2)
                    //loaded the function pointer
                    auto loadMethod = llvm::dyn_cast<llvm::LoadInst>(gen(exp.list[0],env));

                    //FunctionType
                    auto fnTy = llvm::dyn_cast<llvm::FunctionType>(loadMethod->getPointerOperand()->getType()->getContainedType(0)->getContainedType(0));

                    std::vector<llvm::Value*> args{};
                    for(unsigned i = 1; i < exp.list.size(); ++i){
                        auto arg = gen(exp.list[i],env);

                        auto paramType = fnTy->getFunctionParamType(i - 1);

                        if(arg->getType() != paramType){
                            auto bitCastArg = builder->CreateBitCast(arg,paramType);
                            args.push_back(bitCastArg);
                        }else{
                            args.push_back(arg);
                        }
                    }

                    return builder->CreateCall(fnTy,loadMethod,args);
                }
        }

        llvm_unreachable("unreachable code in gen!");
        return builder->getInt32(0 );
    }

    llvm::Function* createFunction(const std::string& fnName,
                                   llvm::FunctionType* fnType,
                                   Env& env){
        //Function prototype
        auto fn = module->getFunction(fnName);
        if(fn == nullptr){
            fn = createFunctionProto(fnName,fnType,env);
        }

        createFunctionBlock(fn);
        return fn;
    }

    void createFunctionBlock(llvm::Function* fn){
        auto entry = createBB("entry",fn);
        builder->SetInsertPoint(entry);
    }

    llvm::BasicBlock* createBB(const std::string& name = "",llvm::Function* fn = nullptr){
        return llvm::BasicBlock::Create(*ctx,name,fn);
    }

    llvm::GlobalVariable* createGlobalVariable(const std::string& name, llvm::Constant* init){
        module->getOrInsertGlobal(name,init->getType());
        auto variable = module->getNamedGlobal(name);
        variable->setAlignment(llvm::MaybeAlign(4));
        variable->setConstant(true);
        variable->setInitializer(init);
        return variable;
    }

    void moduleInit(){
        ctx = std::make_unique<llvm::LLVMContext>();
        module = std::make_unique<llvm::Module>("EvaLLVM",*ctx);
        builder = std::make_unique<llvm::IRBuilder<>>(*ctx);
        varsBuilder = std::make_unique<llvm::IRBuilder<>>(*ctx);
    }

    void saveModuleToFile(const std::string& fileName){
        std::error_code errorCode;
        llvm::raw_fd_ostream outLL(fileName,errorCode);
        module->print(outLL,nullptr);
    }

    llvm::StructType* getClassByName(const std::string& name){
        return llvm::StructType::getTypeByName(*ctx,name);
    }

    void inheritClass(llvm::StructType* derivedCls, llvm::StructType* parentCls){
        auto parentClsInfo = &classMap[parentCls->getName().data()];

        classMap[derivedCls->getName().data()] = {
                /* class */derivedCls,
                /* parent */parentCls,
                /* fields */parentClsInfo->fieldsMap,
                /* methods */parentClsInfo->methodsMap,
        };

    }

    void buildClassInfo(llvm::StructType* cls,const Exp& clsExp,const Env& env){
        auto className = clsExp.list[1].string;
        auto classInfo = &classMap[className];

        assert(classInfo != nullptr);
        auto body = clsExp.list[3];

        for(unsigned i = 0; i < body.list.size(); ++i){
            auto exp = body.list[i];

            if(isVar(exp)){
                auto varNameDecl = exp.list[1];
                auto fieldName = extractVarName(varNameDecl);
                auto fieldType = extractVarType(varNameDecl);
                classInfo->fieldsMap[fieldName] = fieldType;
            }else if(isDef(exp)){
                auto methodName = exp.list[1].string;
                auto fnName = className + "_" + methodName;
                classInfo->methodsMap[methodName] = createFunctionProto(fnName, extractFunctinoType(exp),env);
            }
        }

        //create fields and vTables
        buildClassBody(cls);
    }

    void buildVTables(llvm::StructType* cls){
        std::string className = cls->getName().data();
        auto vTableName = className + "_vTable";

        auto vTableTy = llvm::StructType::getTypeByName(*ctx,vTableName);

        std::vector<llvm::Constant*> vTableMethods;
        std::vector<llvm::Type*> vTableMethodTypes;

        for(auto& methodInfo : classMap[className].methodsMap){
            auto method = methodInfo.second;
            vTableMethods.push_back(method);
            vTableMethodTypes.push_back(method->getType());
        }

        vTableTy->setBody(vTableMethodTypes);

        auto vTableValue = llvm::ConstantStruct::get(vTableTy,vTableMethods);
        createGlobalVariable(vTableName,vTableValue);
    }

    void buildClassBody(llvm::StructType* cls){
        std::string className{cls->getName().data()};

        auto classInfo = &classMap[className];

        //Allocate vTable to set its type in the body
        //The table itself is populated later in buildVTable
        auto vTableName = className + "_vTable";

        //create it in the context -> no depend on any objects
        auto vTableTy = llvm::StructType::create(*ctx,vTableName);

        auto clsFields = std::vector<llvm::Type*>{
            //the first element is always the pointer to the vTable
            vTableTy->getPointerTo(),
        };

        for(const auto& fileInfo : classInfo->fieldsMap){
            clsFields.push_back(fileInfo.second);
        }


        cls->setBody(clsFields,false);

        //Methods
        buildVTables(cls);
    }

    static const size_t  VTABLE_INDEX = 0;
    static const size_t RESERVED_FIELDS_COUNT = 1;

    size_t getFieldIndex(llvm::StructType* _cls, const std::string& fieldName){
        auto fields = &classMap[_cls->getName().data()].fieldsMap;
        auto it = fields->find(fieldName);
        return std::distance(fields->begin(),it) + RESERVED_FIELDS_COUNT;
    }

    size_t getMethodIndex(llvm::StructType* cls_, const std::string& name){
        auto methods = &classMap[cls_->getName().data()].methodsMap;
        auto it = methods->find(name);
        return std::distance(methods->begin(),it);
    }


    bool isTaggedList(const Exp& exp, const std::string& tag){
        return exp.type == ExpType::LIST && exp.list[0].type == ExpType::SYMBOL
                                && exp.list[0].string == tag;
    }
    //(super ...)
    bool isSuper(const Exp& exp){
        return isTaggedList(exp,"super");
    }

    //(prop )
    bool isProp(const Exp& exp){
        return isTaggedList(exp,"prop");
    }

    //(var ...)
    bool isVar(const Exp& exp){
        return isTaggedList(exp,"var");
    }

    //(def ...)
    bool isDef(const Exp& exp){
        return isTaggedList(exp,"def");
    }

    //(new ... )
    bool isNew(const Exp& exp){
        return isTaggedList(exp,"new");
    }


    //currently compiling class
    llvm::StructType* cls = nullptr;

    std::map<std::string,ClassInfo> classMap;

    //global env
    std::shared_ptr<Environment> GlobalEnv;

    //current compling function
    llvm::Function* fn;

    std::unique_ptr<EvaParser> parser;
    //
    std::unique_ptr<llvm::LLVMContext> ctx;

    std::unique_ptr<llvm::Module> module;

    std::unique_ptr<llvm::IRBuilder<>> builder;

    std::unique_ptr<llvm::IRBuilder<>> varsBuilder;
};
#endif //EVA_EVALLVM_H
