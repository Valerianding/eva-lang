//
// Created by Valerian on 2024/2/25.
//

#ifndef EVA_ENVIROMENT_H
#define EVA_ENVIROMENT_H
#include <map>
#include <memory>
#include <utility>
#include <string>
#include "llvm/IR/Value.h"
#include "Logger.h"
class Environment : public std::enable_shared_from_this<Environment>{
public:
    Environment(const std::map<std::string,llvm::Value*>& record,
                const std::shared_ptr<Environment>& parent) : record_(record), parent_(parent){

    }

    llvm::Value* define(const std::string& name, llvm::Value* value){
        record_[name] = value;
        return  value;
    }

    llvm::Value* lookup(const std::string& name){
       return resolve(name)->record_[name];
    }

private:
    std::shared_ptr<Environment> resolve(const std::string& name){
        if(record_.count(name) != 0){
            return shared_from_this();
        }

        if(parent_ == nullptr){
            DIE << "Variable: " << name << "is not defined" << std::endl;
        }
        return parent_->resolve(name);
    }
    /*
     * Bindings storage
     */
    std::map<std::string, llvm::Value*> record_;

    /*
     * Parent link
     */
    std::shared_ptr<Environment> parent_;
};
#endif //EVA_ENVIROMENT_H
