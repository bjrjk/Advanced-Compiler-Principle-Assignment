#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>

#include "Dataflow.h"

using namespace llvm;

typedef Value Object_t;
typedef Value Pointer_t;

static inline bool isObject(Object_t *maybeObject) {
    return isa<Instruction>(maybeObject) || isa<Argument>(maybeObject) || isa<Function>(maybeObject) ||
           isa<ConstantInt>(maybeObject);
}

static inline bool isPointer(Pointer_t *maybePointer) {
    return isObject(maybePointer) || maybePointer->getType()->isPointerTy() &&
                                     (isa<Instruction>(maybePointer) || isa<Function>(maybePointer) ||
                                      isa<ConstantPointerNull>(maybePointer));
}

static inline void assertIsObject(Object_t *maybeObject) {
    assert(isObject(maybeObject));
}

static inline void assertIsPointer(Pointer_t *maybePointer) {
    assert(isPointer(maybePointer));
}

class PointerAnalysisFact {
private:
    std::set<Pointer_t *> pointerContainer;
    std::set<Object_t *> objectContainer;
    std::map<Pointer_t *, std::set<Object_t *>> pointToSetContainer;
    std::set<Pointer_t *> initializedPointerContainer;
    std::map<Pointer_t *, Pointer_t *> structToFieldMapper, fieldToStructMapper;
    std::map<Pointer_t *, Pointer_t *> mockPointerToPointeeMapper, mockPointeeToPointerMapper;
    std::set<Pointer_t *> isMockArrayContainer;
    std::map<Value *, std::set<Value *>> callGraphContainer;
public:
    PointerAnalysisFact() = default;

    PointerAnalysisFact(const PointerAnalysisFact &info) = default;

    bool operator==(const PointerAnalysisFact &fact2) const {
        return pointerContainer == fact2.pointerContainer &&
               objectContainer == fact2.objectContainer &&
               pointToSetContainer == fact2.pointToSetContainer;
    }

    inline bool addPointer(Pointer_t *pointer) {
        assertIsPointer(pointer);
        bool flag = false;

        flag |= pointerContainer.insert(pointer).second;
        flag |= objectContainer.insert(pointer).second; // A pointer is an object
        if (flag)
            pointToSetContainer[pointer].insert(pointer).second;
        // A pointer treated as object should point to itself

        return flag;
    }

    inline bool addObject(Object_t *object) {
        assertIsObject(object);
        bool flag = false;

        flag |= objectContainer.insert(object).second;
        flag |= pointerContainer.insert(object).second; // To process point-to relation, we treat object as pointer
        if (flag)
            pointToSetContainer[object].insert(object).second;
        // When initializing, an object treated as pointer should point to itself

        return flag;
    }

    bool addObjectSet(const std::set<Object_t *> &objectSet) {
        bool flag = false;
        for_each(objectSet.begin(), objectSet.end(), [&](auto *object) {
            flag |= addObject(object);
        });
        return flag;
    }

    bool addPointTo(Pointer_t *pointer, Object_t *object) {
        // The `pointer` must be pointer type. However, the `object` can be either a pointer or value type.
        addPointer(pointer);
        addObject(object);

        return pointToSetContainer[pointer].insert(object).second;
        // Reference: https://zh.cppreference.com/w/cpp/container/set/insert
    }

    bool removePointTo(Pointer_t *pointer, Object_t *object) {
        addPointer(pointer);

        return pointToSetContainer[pointer].erase(object);
    }

    bool removePointToSelf(Pointer_t *pointer) {
        return removePointTo(pointer, pointer);
    }

    bool unionPointToSet(Pointer_t *pointer, const std::set<Object_t *> &externalObjectSet) {
        addPointer(pointer);
        addObjectSet(externalObjectSet);

        std::set<Object_t *> tmpUnionSet;
        auto &internalObjectSet = pointToSetContainer[pointer];

        std::set_union(internalObjectSet.begin(), internalObjectSet.end(),
                       externalObjectSet.begin(), externalObjectSet.end(),
                       std::inserter(tmpUnionSet, tmpUnionSet.end()));

        bool flag = (tmpUnionSet == internalObjectSet);
        if (!flag) std::swap(tmpUnionSet, internalObjectSet);
        return flag;
    }

    bool unionAllPointToSet(const std::set<Object_t *> &externalObjectSet) {
        bool flag = false;

        for (auto &PTSKeyPair: pointToSetContainer) {
            flag |= unionPointToSet(PTSKeyPair.first, externalObjectSet);
        }

        return flag;
    }

    void clearPointToSet(Pointer_t *pointer) {
        addPointer(pointer);

        pointToSetContainer[pointer].clear();
    }

    const std::set<Pointer_t *> &getPointerSet() const {
        return pointerContainer;
    }

    const std::set<Object_t *> &getObjectSet() const {
        return objectContainer;
    }

    const std::set<Object_t *> &getPointToSet(Pointer_t *pointer) {
        addPointer(pointer);
        return pointToSetContainer[pointer];
    }

    const std::set<Object_t *> *getPointToSet(Pointer_t *pointer) const {
        assertIsPointer(pointer);
        if (pointToSetContainer.count(pointer) != 0)
            return &pointToSetContainer.find(pointer)->second;
        else
            return nullptr;
    }

    std::set<Object_t *> getPointToSet(const std::set<Object_t *> &maybePointerSet) {
        std::set<Object_t *> resultSet;
        for (auto *maybePointer: maybePointerSet) {
            if (!isPointer(maybePointer)) continue;
            auto &thisPTS = getPointToSet(maybePointer);
            resultSet.insert(thisPTS.begin(), thisPTS.end());
        }
        return std::move(resultSet);
    }

    bool unionFact(const PointerAnalysisFact &src) {
        bool flag = false;

        pointerContainer.insert(
            src.pointerContainer.begin(),
            src.pointerContainer.end()
        );
        objectContainer.insert(
            src.objectContainer.begin(),
            src.objectContainer.end()
        );
        initializedPointerContainer.insert(
            src.initializedPointerContainer.begin(),
            src.initializedPointerContainer.end()
        );
        structToFieldMapper.insert(
            src.structToFieldMapper.begin(),
            src.structToFieldMapper.end()
        );
        fieldToStructMapper.insert(
            src.fieldToStructMapper.begin(),
            src.fieldToStructMapper.end()
        );
        mockPointerToPointeeMapper.insert(
            src.mockPointerToPointeeMapper.begin(),
            src.mockPointerToPointeeMapper.end()
        );
        mockPointeeToPointerMapper.insert(
            src.mockPointeeToPointerMapper.begin(),
            src.mockPointeeToPointerMapper.end()
        );
        isMockArrayContainer.insert(
            src.isMockArrayContainer.begin(),
            src.isMockArrayContainer.end()
        );
        for (auto &externalPTSPair: src.pointToSetContainer) {
            flag |= unionPointToSet(externalPTSPair.first, externalPTSPair.second);
        }

        return flag;
    }

    void setTop() {
        for (auto *pointer: pointerContainer) {
            pointToSetContainer[pointer] = objectContainer;
        }
    }

    inline bool trySetPointerInitialized(Pointer_t *pointer) {
        if (!isa<AllocaInst>(pointer)) return false;
        // When the pointer isn't initialized, add pointer to initializedPointerContainer and return true;
        // When the pointer got initialized (pointer in initializedPointerContainer), return false.
        if (!initializedPointerContainer.count(pointer)) {
            initializedPointerContainer.insert(pointer);
            return true;
        }
        return false;
    }

    bool isStruct(Pointer_t *maybeStructPtr) const {
        return structToFieldMapper.count(maybeStructPtr);
    }

    bool isField(Pointer_t *maybeFieldPtr) const {
        return fieldToStructMapper.count(maybeFieldPtr);
    }

    bool isArray(Pointer_t *maybeArrayPtr) const {
        return isMockArrayContainer.count(maybeArrayPtr);
    }

    bool isAllPointer2Struct(const std::set<Pointer_t *> &maybePtr2StructSet) const {
        bool flag = true;
        for (auto *maybePtr2Struct: maybePtr2StructSet) {
            auto *maybeStructSet = getPointToSet(maybePtr2Struct);
            flag &= isAllStruct(*maybeStructSet);
        }
        return flag;
    }

    bool isAllStruct(const std::set<Pointer_t *> &maybeStructPtrSet) const {
        bool flag = true;
        for (auto *maybeStructPtr: maybeStructPtrSet) {
            flag &= isStruct(maybeStructPtr);
        }
        return flag;
    }

    bool isAllField(const std::set<Pointer_t *> &maybeFieldPtrSet) const {
        bool flag = true;
        for (auto *maybeFieldPtr: maybeFieldPtrSet) {
            flag &= isField(maybeFieldPtr);
        }
        return flag;
    }

    bool isAllStructFieldHybrid(const std::set<Pointer_t *> &maybeStructFieldPtrSet) const {
        bool flag = true;
        for (auto *maybeStructFieldPtr: maybeStructFieldPtrSet) {
            flag &= isStruct(maybeStructFieldPtr) | isField(maybeStructFieldPtr);
        }
        return flag;
    }

    bool isAllArray(const std::set<Pointer_t *> &maybeArrayPtrSet) const {
        bool flag = true;
        for (auto *maybeArrayPtr: maybeArrayPtrSet) {
            flag &= isArray(maybeArrayPtr);
        }
        return flag;
    }

    bool isAllNonArray(const std::set<Pointer_t *> &maybeNonArrayPtrSet) const {
        bool flag = true;
        for (auto *maybeNonArrayPtr: maybeNonArrayPtrSet) {
            flag &= !isArray(maybeNonArrayPtr);
        }
        return flag;
    }

    bool isAllNonStructRelated(const std::set<Pointer_t *> &maybeNonStructRelatedPtrSet) const {
        bool flag = true;
        for (auto *maybeNonStructPtr: maybeNonStructRelatedPtrSet) {
            flag &= !isStruct(maybeNonStructPtr) && !isField(maybeNonStructPtr);
        }
        return flag;
    }

    Pointer_t *getStructField(Pointer_t *structPtr) const {
        assert(structToFieldMapper.count(structPtr) > 0);
        return structToFieldMapper.find(structPtr)->second;
    }

    std::set<Pointer_t *> getAllStructField(const std::set<Pointer_t *> &structPtrSet) const {
        std::set<Object_t *> toUnionSet;
        for (auto *object: structPtrSet) {
            auto *RHSField = getStructField(object);
            toUnionSet.insert(RHSField);
        }
        return toUnionSet;
    }

    void setStructField(Pointer_t *structPtr, Pointer_t *fieldPtr) {
        structToFieldMapper[structPtr] = fieldPtr;
        fieldToStructMapper[fieldPtr] = structPtr;
    }

    Pointer_t *getMockPointerPointee(Pointer_t *pointer) const {
        assert(mockPointerToPointeeMapper.count(pointer) > 0);
        return mockPointerToPointeeMapper.find(pointer)->second;
    }

    std::set<Pointer_t *> getAllMockPointerPointee(const std::set<Pointer_t *> &mockPointerSet) const {
        std::set<Object_t *> toUnionSet;
        for (auto *object: mockPointerSet) {
            auto *RHSPointee = getMockPointerPointee(object);
            toUnionSet.insert(RHSPointee);
        }
        return toUnionSet;
    }

    bool setMockPointerPointee(Pointer_t *pointer, Pointer_t *pointee) {
        if (mockPointerToPointeeMapper.count(pointer) == 0) {
            mockPointerToPointeeMapper[pointer] = pointee;
            mockPointeeToPointerMapper[pointee] = pointer;
            return true;
        } else {
            assert(mockPointerToPointeeMapper[pointer] == pointee);
            assert(mockPointeeToPointerMapper[pointee] == pointer);
            return false;
        }
    }

    bool setIsArray(Pointer_t *arrayPtr) {
        return isMockArrayContainer.insert(arrayPtr).second;
    }

    /* CallEdgeType:
     * CallBase -> Function, FunctionPtr
     * FunctionPtr -> Function
     */
    void addCallEdge(Value *callBase, Value *function) {
        assert(isa<CallBase>(callBase) ||
               !isa<CallBase>(callBase) && isa<Function>(function));
        callGraphContainer[callBase].insert(function);
    }

    const std::map<Value *, std::set<Value *>> &getCallGraph() const {
        return callGraphContainer;
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const PointerAnalysisFact &info) {
    char buf[1024]; // Warning: Buffer Overflow
    int counter;

    counter = 0;
    out << "\t\t[!] Pointer Container: \n";
    for (auto *pointer: info.getPointerSet()) {
        sprintf(buf, "%20s(%p)\t\t\t\t", pointer->getName().data(), pointer);
        out << buf;
        if (counter == 3) out << "\n";
        counter = (counter + 1) % 4;
    }
    out << "\n";

    counter = 0;
    out << "\t\t[!] Object Container: \n";
    for (auto *object: info.getObjectSet()) {
        sprintf(buf, "%20s(%p)\t\t\t\t", object->getName().data(), object);
        out << buf;
        if (counter == 3) out << "\n";
        counter = (counter + 1) % 4;
    }
    out << "\n";

    out << "\t\t[!] Point-to Relation: \n";
    for (auto *pointer: info.getPointerSet()) {
        auto *PTS = info.getPointToSet(pointer);
        if (PTS == nullptr) continue;
        sprintf(buf, "\t\t\t[--] Pointer: %s(%p), Pointee: \n", pointer->getName().data(), pointer);
        out << buf;
        counter = 0;
        for (auto *object: *PTS) {
            sprintf(buf, "%20s(%p)\t\t\t\t", object->getName().data(), object);
            out << buf;
            if (counter == 3) out << "\n";
            counter = (counter + 1) % 4;
        }
        out << "\n";
    }

    return out;
}


class PointerAnalysisVisitor : public DataflowVisitor<struct PointerAnalysisFact> {
public:
    PointerAnalysisVisitor() = default;

    void merge(PointerAnalysisFact *dest, const PointerAnalysisFact &src) override {
        dest->unionFact(src);
    }

    static inline void transferFactReference(PointerAnalysisFact *fact,
                                             Pointer_t *LHS, Object_t *RHS) { // LHS = &RHS
        assertIsPointer(LHS);
        assertIsObject(RHS);

        fact->clearPointToSet(LHS);
        fact->addPointTo(LHS, RHS);
    }

    static inline void transferFactNonClearAssign(PointerAnalysisFact *fact,
                                                  Pointer_t *LHS, Pointer_t *RHS) { // LHS = RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto RHS_PTS = fact->getPointToSet(RHS);
        fact->unionPointToSet(LHS, RHS_PTS);
        fact->removePointTo(LHS, LHS);
    }

    static inline void transferFactAssign(PointerAnalysisFact *fact,
                                          Pointer_t *LHS, Pointer_t *RHS) { // LHS = RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto RHS_PTS = fact->getPointToSet(RHS);
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, RHS_PTS);
    }

    static inline void transferFactLoad(PointerAnalysisFact *fact,
                                        Pointer_t *LHS, Pointer_t *RHS) { // LHS = *RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &RHS_PTS = fact->getPointToSet(RHS);
        auto RHS_PTS_PTS = fact->getPointToSet(RHS_PTS);
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, RHS_PTS_PTS);
    }

    static inline void transferFactStore(PointerAnalysisFact *fact,
                                         Pointer_t *LHS, Pointer_t *RHS) { // *LHS = RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS.begin();
                transferFactAssign(fact, onlyPointee, RHS);
                break;
            }
            default: {
                auto RHS_PTS = fact->getPointToSet(RHS);
                for (auto *pointer: LHS_PTS) {
                    if (!RHS_PTS.empty()) fact->removePointToSelf(pointer);
                    fact->unionPointToSet(pointer, RHS_PTS);
                }
                break;
            }
        }
    }

    static inline void transferFactStoreNull(PointerAnalysisFact *fact,
                                             Pointer_t *LHS) { // *LHS = nullptr
        assertIsPointer(LHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS.begin();
                fact->clearPointToSet(onlyPointee);
                break;
            }
        }
    }

    static inline void transferFactArrayStoreNull(PointerAnalysisFact *fact,
                                                  Pointer_t *LHS) { // LHS[] = nullptr
        assertIsPointer(LHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        assert(fact->isAllArray(LHS_PTS));
        auto LHS_PTS_Elem = fact->getAllMockPointerPointee(LHS_PTS);

        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS_Elem.begin();
                fact->clearPointToSet(onlyPointee);
                break;
            }
        }
    }

    static inline void transferFactLoadStoreField(PointerAnalysisFact *fact,
                                                  Pointer_t *LHS, Pointer_t *RHS) { // *LHS = *RHS (memcpy)
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS.begin();
                transferFactLoad(fact, onlyPointee, RHS);
                break;
            }
            default: {
                auto RHS_PTS = fact->getPointToSet(RHS);
                auto RHS_PTS_PTS = fact->getPointToSet(RHS_PTS);
                for (auto *pointer: LHS_PTS) {
                    fact->unionPointToSet(pointer, RHS_PTS_PTS);
                }
                break;
            }
        }
    }

    static inline void transferFactLoadStoreStruct(PointerAnalysisFact *fact,
                                                   Pointer_t *LHS,
                                                   Pointer_t *RHS) { // *LHS._ = *RHS (memcpy) (*LHS is object)
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto LHS_PTS_Field = fact->getAllStructField(fact->getPointToSet(LHS));
        switch (LHS_PTS_Field.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS_Field.begin();
                transferFactLoad(fact, onlyPointee, RHS);
                break;
            }
            default: {
                auto RHS_PTS = fact->getPointToSet(RHS);
                auto RHS_PTS_PTS = fact->getPointToSet(RHS_PTS);
                for (auto *pointer: LHS_PTS_Field) {
                    fact->unionPointToSet(pointer, RHS_PTS_PTS);
                }
                break;
            }
        }
    }

    static inline void transferFactLoadStorePointer2Struct(PointerAnalysisFact *fact,
                                                           Pointer_t *LHS,
                                                           Pointer_t *RHS) {
        // **LHS._ = *RHS (memcpy) (LHS is pointer to struct, *LHS is struct object)
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        for (auto *structPtr: LHS_PTS) {
            transferFactLoadStoreStruct(fact, structPtr, RHS);
        }
    }

    static inline void transferFactReferenceStore(PointerAnalysisFact *fact,
                                                  Pointer_t *LHS, Pointer_t *RHS) { // *LHS = &RHS
        assertIsPointer(LHS);
        assertIsPointer(RHS);

        auto &LHS_PTS = fact->getPointToSet(LHS);
        switch (LHS_PTS.size()) {
            case 0: {
                fact->setTop();
                break;
            }
            case 1: {
                Pointer_t *onlyPointee = *LHS_PTS.begin();
                transferFactReference(fact, onlyPointee, RHS);
                break;
            }
            default: {
                std::set<Pointer_t *> RHS_PTS;
                RHS_PTS.insert(RHS);
                for (auto *pointer: LHS_PTS) {
                    fact->unionPointToSet(pointer, RHS_PTS);
                }
                break;
            }
        }
    }

    static inline void transferFactFieldReference(PointerAnalysisFact *fact,
                                                  Pointer_t *LHS,
                                                  Pointer_t *RHS) { // LHS = &RHS.field (RHS is a struct)
        auto *RHSField = fact->getStructField(RHS);
        fact->clearPointToSet(LHS);
        fact->addPointTo(LHS, RHSField);
    }

    static inline void transferFactFieldAssign(PointerAnalysisFact *fact,
                                               Pointer_t *LHS,
                                               Pointer_t *RHS) { // LHS = RHS.field (RHS point to a struct)
        auto toUnionSet = fact->getAllStructField(fact->getPointToSet(RHS));
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, toUnionSet);
    }

    static inline void transferFactFieldLoad(PointerAnalysisFact *fact,
                                             Pointer_t *LHS,
                                             Pointer_t *RHS) { // LHS = (*RHS).field (RHS point to pointers of struct)
        auto toUnionSet = fact->getAllStructField(fact->getPointToSet(fact->getPointToSet(RHS)));
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, toUnionSet);
    }

    static inline void transferFactStructFieldHybrid(PointerAnalysisFact *fact,
                                                     Pointer_t *LHS,
                                                     Pointer_t *RHS) {
        auto RHS_PTS = fact->getPointToSet(RHS);
        fact->clearPointToSet(LHS);
        for (auto *RHSPointee: RHS_PTS) {
            if (fact->isStruct(RHSPointee)) {
                auto structField = fact->getStructField(RHSPointee);
                fact->addPointTo(LHS, structField);
            } else if (fact->isField(RHSPointee)) {
                fact->addPointTo(LHS, RHSPointee);
            } else {
                assert(false);
            }
        }
    }

    static inline void transferFactArrayAssign(PointerAnalysisFact *fact,
                                               Pointer_t *LHS,
                                               Pointer_t *RHS) { // LHS = RHS[] (RHS point to an array)
        auto toUnionSet = fact->getAllMockPointerPointee(fact->getPointToSet(RHS));
        fact->clearPointToSet(LHS);
        fact->unionPointToSet(LHS, toUnionSet);
    }

    static Instruction *createNewInst(const Twine &name, Type *type) {
        Instruction *inst = new AllocaInst(type, 0);
        inst->setName(name);
        return inst;
    }

    static Instruction *createStructField(Value *parent) {
        // Create a unified field object, represented as i8 *
        return createNewInst(parent->getName() + "._", PointerType::get(IntegerType::get(parent->getContext(), 8), 0));
    }

    static void mockObject(Value *maybeMockPointer, PointerAnalysisFact *fact, Type *type) {
        Type *curType = type;
        Value *parent = maybeMockPointer;
        std::string nameSuffix;
        while (curType->isPointerTy() || curType->isArrayTy()) {
            if (curType->isPointerTy()) {
                curType = curType->getPointerElementType();
                nameSuffix = ".p";
            } else if (curType->isArrayTy()) {
                curType = curType->getArrayElementType();
                nameSuffix = ".a";
            } else {
                assert(false);
            }
            Instruction *mockObject = createNewInst(parent->getName() + nameSuffix, curType);
            fact->addObject(mockObject);
            transferFactReference(fact, parent, mockObject);
            if (nameSuffix == ".a") fact->setIsArray(mockObject);
            fact->setMockPointerPointee(parent, mockObject);
            parent = mockObject;
        }

        if (curType->isStructTy()) {
            Instruction *field = createStructField(parent);
            fact->addObject(field);
            transferFactReference(fact, parent, field);
            fact->setStructField(parent, field);
        }
    }

    static void mockObject(Value *maybeMockPointer, PointerAnalysisFact *fact) {
        mockObject(maybeMockPointer, fact, maybeMockPointer->getType());
    }

    static void clearPointToGraphSubNodePointee(PointerAnalysisFact *fact, CallBase *callInst) {
        unsigned int argNum = callInst->getNumArgOperands();
        std::set<Pointer_t *> toClearPointee, workList;
        for (int i = 0; i < argNum; i++) {
            auto *curArg = callInst->getArgOperand(i);
            for (auto *pointee: fact->getPointToSet(curArg)) {
                workList.insert(pointee);
                toClearPointee.insert(pointee);
            }
        }
        while (!workList.empty()) {
            auto *curPointer = *workList.begin();
            workList.erase(workList.begin());
            for (auto *pointee: fact->getPointToSet(curPointer)) {
                if (!toClearPointee.count(pointee)) {
                    workList.insert(pointee);
                    toClearPointee.insert(pointee);
                }
            }
        }
        for (auto *pointer: toClearPointee) {
            fact->clearPointToSet(pointer);
        }
    }

    // Find storage position of a called operand (function pointer).
    static Value *getCalledOperandStorage(PointerAnalysisFact *fact, Value *calledOperand) {
        auto *upstreamLoadInst = cast<LoadInst>(calledOperand);
        auto *storagePointer = upstreamLoadInst->getPointerOperand();
        auto &PTS = fact->getPointToSet(storagePointer);
        if (PTS.size() == 1) {
            return *PTS.begin();
        } else {
            return calledOperand;
        }
    }

    static void transferInstAlloca(AllocaInst *allocaInst, PointerAnalysisFact *fact) {
        // Heap Abstraction: Allocation Site
        if (allocaInst->getAllocatedType()->isPointerTy()) {
            // Pointer Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Pointer Allocation.\n");
#endif
            fact->addPointer(allocaInst);
        } else if (allocaInst->getAllocatedType()->isStructTy()) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Struct Allocation.\n");
#endif
            fact->addObject(allocaInst);
            mockObject(allocaInst, fact, allocaInst->getAllocatedType());
        } else if (allocaInst->getAllocatedType()->isArrayTy()) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Array Allocation.\n");
#endif
            fact->addObject(allocaInst);
            mockObject(allocaInst, fact, allocaInst->getAllocatedType());
        } else {
            // Object Allocation
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Object Allocation.\n");
#endif
            fact->addObject(allocaInst);
        }
    }

    static void transferInstLoad(LoadInst *loadInst, PointerAnalysisFact *fact) {
        auto *LHS = loadInst;
        auto *RHS = loadInst->getPointerOperand();
        if (isa<AllocaInst>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Load Operation (Alloca Load, Assign): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactAssign(fact, LHS, RHS);
        } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Load Operation (Normal Load): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactLoad(fact, LHS, RHS);
        }
    }

    static void transferInstStore(StoreInst *storeInst, PointerAnalysisFact *fact, bool isEntrypoint) {
        auto *LHS = storeInst->getPointerOperand();
        auto *RHS = storeInst->getValueOperand();
        if (auto *argRHS = dyn_cast<Argument>(RHS)) {
            // Handle pointer-to relation of function argument
            bool isIntraProcedure = false;
#ifdef INTRA_PROCEDURE_ANALYSIS
            isIntraProcedure = true;
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Intra-Procedure Analysis.\n");
#endif
#else
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t\t[-] Inter-Procedure Analysis.\n");
#endif
#endif
            if (isIntraProcedure ||
                isEntrypoint) { // Intra-Procedure Analysis or Entrypoint of Inter-Procedure Analysis
                fact->addObject(argRHS);
                if (isa<AllocaInst>(LHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of Store Operation (Function Argument Mocking, Reference): %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, argRHS->getName().data(), argRHS);
#endif
                    transferFactReference(fact, LHS, argRHS);
                } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of Store Operation (Function Argument Mocking, Store): %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, argRHS->getName().data(), argRHS);
#endif
                    transferFactStore(fact, LHS, argRHS);
                }
                // Mock for structure
                mockObject(argRHS, fact);
            } else { // Non-Entrypoint of Inter-Procedure Analysis
                if (isa<AllocaInst>(LHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of Store Operation (Function Argument Transferring, Assign): %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, argRHS->getName().data(), argRHS);
#endif
                    transferFactAssign(fact, LHS, argRHS);
                } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of Store Operation (Function Argument Transferring, Store): %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, argRHS->getName().data(), argRHS);
#endif
                    transferFactStore(fact, LHS, argRHS);
                }
            }
        } else if (isa<AllocaInst>(LHS) && isa<AllocaInst>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Store Operation (Alloca Reference Assign to Alloca Content, Reference): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactReference(fact, LHS, RHS);
        } else if (auto *allocaRHS = dyn_cast<AllocaInst>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of Store Operation (Alloca Reference Store, ReferenceStore): %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            // Handle alloca address reference store
            transferFactReferenceStore(fact, LHS, allocaRHS);
        } else if (auto *allocaLHS = dyn_cast<AllocaInst>(LHS)) { // fact->trySetPointerInitialized(LHS)
            if (isa<ConstantInt>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of Store Operation (Constant Assign, omitted): %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            } else if (!isa<ConstantPointerNull>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of Store Operation (Assign to Alloca Content, Assign): %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                // Handle pointer assign
                transferFactAssign(fact, allocaLHS, RHS);
            } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of Store Operation (NullPtr Assign to Alloca Content, Clear): %s(%p) <- NULL.\n",
                        LHS->getName().data(), LHS);
#endif
                fact->clearPointToSet(allocaLHS);
            }
        } else {
            if (!isa<ConstantPointerNull>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of Store Operation (Normal Variable, Store): %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactStore(fact, LHS, RHS);
            } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of Store Operation (Nullptr Store to Normal Variable, StoreNull): %s(%p) <- NULL.\n",
                        LHS->getName().data(), LHS);
#endif
                transferFactStoreNull(fact, LHS);
            }
        }
    }

    static void transferInstGetElemPtr(GetElementPtrInst *getElemPtrInst, PointerAnalysisFact *fact) {
        // `getelementptr %struct, src` is computing an address of structure's data field
        auto *LHS = getElemPtrInst;
        auto *RHS = getElemPtrInst->getPointerOperand();
        if (LHS->getSourceElementType()->isStructTy()) {
            if (isa<AllocaInst>(RHS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of GetElementPtr(Struct Alloca, FieldReference) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactFieldReference(fact, LHS, RHS);
            } else {
                auto &RHS_PTS = fact->getPointToSet(RHS);
                bool allStruct = fact->isAllStruct(RHS_PTS);
                bool allField = fact->isAllField(RHS_PTS);
                bool allStructFieldHybrid = fact->isAllStructFieldHybrid(RHS_PTS);
                bool allNonStructRelated = fact->isAllNonStructRelated(RHS_PTS);
                assert(allStruct && !allField && allStructFieldHybrid && !allNonStructRelated ||
                       !allStruct && allField && allStructFieldHybrid && !allNonStructRelated ||
                       !allStruct && !allField && allStructFieldHybrid && !allNonStructRelated ||
                       !allStruct && !allField && !allStructFieldHybrid && allNonStructRelated);
                if (allStruct) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of GetElementPtr(Struct TempReg, FieldAssign) Operation: %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                    transferFactFieldAssign(fact, LHS, RHS);
                } else if (allField) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of GetElementPtr(Struct TempReg, Assign) Operation: %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                    transferFactAssign(fact, LHS, RHS);
                } else if(allStructFieldHybrid) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of GetElementPtr(Struct TempReg, StructFieldHybrid) Operation: %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                    transferFactStructFieldHybrid(fact, LHS, RHS);
                } else if (allNonStructRelated) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Transfer Fact of GetElementPtr(Struct TempReg, FieldLoad) Operation: %s(%p) <- %s(%p).\n",
                            LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                    transferFactFieldLoad(fact, LHS, RHS);
                } else {
                    assert(false);
                }
            }
        } else { // Handle Array
            if (LHS->getName().startswith("arrayid")) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of GetElementPtr(ArrayID, ArrayAssign) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactArrayAssign(fact, LHS, RHS);
            } else if (LHS->getName().startswith("arraydecay")) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of GetElementPtr(ArrayDecay, Assign) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactAssign(fact, LHS, RHS);
            }

        }
    }

    static void transferInstBitCast(BitCastInst *bitCastInst, PointerAnalysisFact *fact) {
        auto *LHS = bitCastInst;
        auto *RHS = bitCastInst->getOperand(0);

        if (auto *callBase = dyn_cast<CallBase>(RHS)) {
            if (callBase->getCalledOperand()->getName().equals("malloc")) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of BitCast(malloc) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                // Handle malloc mock
                mockObject(LHS, fact);
            }
        } else {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Transfer Fact of BitCast(Assign) Operation: %s(%p) <- %s(%p).\n",
                    LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
            transferFactAssign(fact, LHS, RHS);
        }
    }

    static void transferInstCall(CallBase *callInst, PointerAnalysisFact *fact,
                                 typename DataflowResult<PointerAnalysisFact>::Type *resultContainer,
                                 DataflowVisitor<PointerAnalysisFact> *visitor) {
        auto functionName = callInst->getCalledOperand()->getName();
        if (functionName.startswith("llvm.dbg")) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr,
                    "\t\t\t[-] Debug Call, return.\n");
#endif
            return;
        } else if (functionName.startswith("llvm.memcpy")) {
            auto *LHS = callInst->getOperand(0);
            auto *RHS = callInst->getOperand(1);
            // TODO: Implement copy semantic
            auto &LHS_PTS = fact->getPointToSet(LHS);
            if (fact->isAllStruct(LHS_PTS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of llvm.memcpy(LoadStoreStruct) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactLoadStoreStruct(fact, LHS, RHS);
            } else if (fact->isAllField(LHS_PTS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of llvm.memcpy(LoadStoreField) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactLoadStoreField(fact, LHS, RHS);
            } else if (fact->isAllPointer2Struct(LHS_PTS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr,
                        "\t\t\t[-] Transfer Fact of llvm.memcpy(LoadStorePointer2Struct) Operation: %s(%p) <- %s(%p).\n",
                        LHS->getName().data(), LHS, RHS->getName().data(), RHS);
#endif
                transferFactLoadStorePointer2Struct(fact, LHS, RHS);
            } else {
                assert(false);
            }
        } else if (functionName.startswith("llvm.memset")) {
            auto *LHS = callInst->getOperand(0);
            auto *RHS = callInst->getOperand(1);
            if (auto *constInt = dyn_cast<ConstantInt>(RHS)) {
                if (constInt->getSExtValue() == 0) {
                    auto &LHS_PTS = fact->getPointToSet(LHS);
                    if (fact->isAllArray(LHS_PTS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr,
                                "\t\t\t[-] Transfer Fact of llvm.memset(Array, ArrayStoreNull) Operation: %s(%p).\n",
                                LHS->getName().data(), LHS);
#endif
                        transferFactArrayStoreNull(fact, LHS);
                    } else if (fact->isAllNonArray(LHS_PTS)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
                        fprintf(stderr,
                                "\t\t\t[-] Transfer Fact of llvm.memset(Non-Array, StoreNull) Operation: %s(%p).\n",
                                LHS->getName().data(), LHS);
#endif
                        transferFactStoreNull(fact, LHS);
                    } else {
                        assert(false);
                    }
                } else {
                    assert(false);
                }
            } else {
                assert(false);
            }
        } else { // Handle non-intrinsic functions
            // Add to CallGraph
            auto *calledOperand = callInst->getCalledOperand();
            std::set<Function *> calledFunctionSet;
            if (auto *function = dyn_cast<Function>(calledOperand)) {
                fact->addCallEdge(callInst, function); // CallBase -> Function
                calledFunctionSet.insert(function);
            } else {
                auto *calledOperandStorage = getCalledOperandStorage(fact, calledOperand);
                fact->addCallEdge(callInst, calledOperandStorage); // CallBase -> FunctionPtr
                auto &PTS = fact->getPointToSet(calledOperand);
                for (auto *maybeFunction: PTS) {
                    if (auto *function = dyn_cast<Function>(maybeFunction)) {
                        fact->addCallEdge(calledOperandStorage, function); // FunctionPtr -> Function
                        calledFunctionSet.insert(function);
                    }
                }
            }
            if (!functionName.startswith("malloc")) {
#ifndef INTRA_PROCEDURE_ANALYSIS
                unsigned int argNum = callInst->getNumArgOperands();
                for (auto *calledFunction: calledFunctionSet) {
                    // Handle argument-parameter transferring
                    for (int i = 0; i < argNum; i++) {
                        auto *curArg = callInst->getArgOperand(i);
                        auto *curPara = calledFunction->getArg(i);
                        transferFactNonClearAssign(fact, curPara, curArg);
                    }
                }
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "\t\t\t[-] Inter-Procedure argument-parameter transferred fact:\n");
                printDataflowFact(errs(), *fact);
#endif
                for (auto *calledFunction: calledFunctionSet) {
                    // Analyze function
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Inter-Procedure start to analyze: %s(%p).\n",
                            calledFunction->getName().data(), calledFunction);
#endif
                    analyzeForward(calledFunction, visitor, resultContainer, *fact, false);
#ifdef ASSIGNMENT_DEBUG_DUMP
                    fprintf(stderr,
                            "\t\t\t[-] Inter-Procedure end analyzing: %s(%p).\n",
                            calledFunction->getName().data(), calledFunction);
#endif
                }
                clearPointToGraphSubNodePointee(fact, callInst);
                for (auto *calledFunction: calledFunctionSet) {
                    // Handle return value transferring
                    BasicBlock *calledFuncRetBasicBlock;
                    ReturnInst *calledFuncRetInst = [=](BasicBlock **calledFuncRetBasicBlock) {
                        for (auto &basicBlock: *calledFunction) {
                            for (auto &inst: basicBlock) {
                                if (auto *retInst = dyn_cast<ReturnInst>(&inst)) {
                                    *calledFuncRetBasicBlock = &basicBlock;
                                    return retInst;
                                }
                            }
                        }
                        return static_cast<ReturnInst *>(nullptr);
                    }(&calledFuncRetBasicBlock);
                    fact->unionFact((*resultContainer)[calledFuncRetBasicBlock].output);
                    if (calledFuncRetInst && calledFuncRetInst->getReturnValue()) {
                        transferFactNonClearAssign(fact, callInst, calledFuncRetInst->getReturnValue());
                    }
                }
#ifdef ASSIGNMENT_DEBUG_DUMP
                fprintf(stderr, "\t\t\t[-] Inter-Procedure return value transferred fact:\n");
                printDataflowFact(errs(), *fact);
#endif
#endif
            }
        }
    }

    void transferInst(Instruction *inst, PointerAnalysisFact *fact,
                      InterAnalysisInfo<PointerAnalysisFact> &interAnalysisInfo) override {
        if (auto *allocaInst = dyn_cast<AllocaInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "AllocaInst", allocaInst);
            inst->dump();
#endif
            transferInstAlloca(allocaInst, fact);
        } else if (auto *storeInst = dyn_cast<StoreInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "StoreInst", storeInst);
            inst->dump();
#endif
            transferInstStore(storeInst, fact, interAnalysisInfo.isEntryPoint);
        } else if (auto *loadInst = dyn_cast<LoadInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "LoadInst", loadInst);
            inst->dump();
#endif
            transferInstLoad(loadInst, fact);
        } else if (auto *getElemPtrInst = dyn_cast<GetElementPtrInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "GetElementPtrInst", getElemPtrInst);
            inst->dump();
#endif
            transferInstGetElemPtr(getElemPtrInst, fact);
        } else if (auto *bitCastInst = dyn_cast<BitCastInst>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "BitCastInst", bitCastInst);
            inst->dump();
#endif
            transferInstBitCast(bitCastInst, fact);
        } else if (auto *callBase = dyn_cast<CallBase>(inst)) {
#ifdef ASSIGNMENT_DEBUG_DUMP
            fprintf(stderr, "\t\t[*] Handle %s instruction %p:", "CallInst", callBase);
            inst->dump();
#endif
            transferInstCall(callBase, fact, interAnalysisInfo.resultContainer, interAnalysisInfo.visitor);
        }
#ifdef ASSIGNMENT_DEBUG_DUMP
        printDataflowFact<PointerAnalysisFact>(errs(), *fact);
#endif
    }
};


class PointerAnalysis : public FunctionPass {
public:
    static char ID;

    PointerAnalysis() : FunctionPass(ID) {}

    static void labelAnonymousInstruction(Function &function) {
        char buf[1024]; // Warning: Buffer Overflow
        int counter = 0;

        for (auto &basicBlock: function) {
            for (auto &inst: basicBlock) {
                if (!inst.getType()->isVoidTy()) {
                    if (inst.getName().equals("")) {
                        sprintf(buf, "%s_t%d", function.getName().data(), counter++);
                    } else {
                        sprintf(buf, "%s_%s", function.getName().data(), inst.getName().data());
                    }
                    inst.setName(buf);
                }
            }
        }

        for (auto &argument: function.args()) {
            sprintf(buf, "arg:%s_%s", function.getName().data(), argument.getName().data());
            argument.setName(buf);
        }
    }

    static void analyzeFunction(Function &F, PointerAnalysisVisitor *visitor,
                                DataflowResult<PointerAnalysisFact>::Type *result,
                                const PointerAnalysisFact &initVal, bool isEntrypoint) {
#ifdef ASSIGNMENT_DEBUG_DUMP
        labelAnonymousInstruction(F);
        fprintf(stderr, "[+] Analyzing Function %s %p, IR:\n", F.getName().data(), &F);
        stderrCyanBackground();
        F.dump();
        stderrNormalBackground();
#endif
        analyzeForward(&F, visitor, result, initVal, isEntrypoint);
#ifdef ASSIGNMENT_DEBUG_DUMP
        printDataflowResult<PointerAnalysisFact>(errs(), *result);
#endif
    }

    bool runOnFunction(Function &F) override {
        PointerAnalysisVisitor visitor;
        DataflowResult<PointerAnalysisFact>::Type result;
        PointerAnalysisFact initVal;

        analyzeFunction(F, &visitor, &result, initVal, true);
        return false;
    }
};



