#include "game_value_array_native.h"
#include <stdexcept>
namespace dokodemo::recovered {
namespace {
using script::Value;using script::Vm;
void Need(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int32_t Number(const Value& v){
    if(v.kind==Value::Kind::ArrayHeader)return int32_t(v.word&0xFFF);
    Need((v.kind==Value::Kind::Scalar||v.kind==Value::Kind::Timestamp)&&v.word>>28<=3,"Array numeric/string/reference conversion not bound");
    const auto p=v.word&0x0FFFFFFF;return int32_t(p&0x08000000?p|0xF0000000:p);
}
Value Element(Vm& vm,const Value& array,uint32_t index){return vm.load_reference(vm.indexed_reference(array,index));}
bool Equal(const Value& a,const Value& b){
    if(a.kind==Value::Kind::Reference||a.kind==Value::Kind::ArrayHeader)return false;
    if(a.kind==Value::Kind::String)return b.kind==Value::Kind::String&&a.cp932==b.cp932;
    if(a.kind==Value::Kind::Scalar&&a.word>>28==0)return b.kind==Value::Kind::Scalar&&b.word>>28==0;
    if(a.kind==Value::Kind::Scalar&&a.word>>28==1)return b.kind==Value::Kind::Scalar&&b.word>>28==1&&(a.word&1)==(b.word&1);
    return a.kind==b.kind&&a.word==b.word;
}
void Action(Vm& vm,const Value& object,int32_t index,int32_t op,const Value& operations,uint32_t argument,GameValueArrayResult& s){
    Need(index>=0&&op>=0&&op<=16,"Array action triple domain");if(op==16)return;
    Need(op<=3,"Unrecovered array action selector");
    auto target=vm.indexed_reference(object,uint32_t(index));const auto input=Element(vm,operations,argument);auto value=input;
    if(op){const auto rhs=Number(input),lhs=Number(vm.load_reference(target));value=Value::integer(op==1?lhs|rhs:op==2?lhs&rhs:lhs^rhs);}
    vm.store_reference(target,std::move(value));++s.writes;
}
bool Predicate(Vm& vm,const Value& object,int32_t index,int32_t op,const Value& operations,uint32_t argument,GameValueArrayResult& s){
    Need(index>=0&&op>=0&&op<=16,"Array predicate triple domain");if(op==16)return true;
    const auto a=Element(vm,object,uint32_t(index)),b=Element(vm,operations,argument);++s.predicates;
    if(op==0)return Equal(a,b);if(op==1)return !Equal(a,b);
    const auto x=Number(a),y=Number(b);
    switch(op){case 2:return x>y;case 3:return x>=y;case 4:return x<y;case 5:return x<=y;
    case 6:return (uint32_t(x)&uint32_t(y))==uint32_t(Number(Element(vm,operations,argument)));
    case 7:return (uint32_t(x)&uint32_t(y))==0;case 8:return (uint32_t(x)&uint32_t(y))!=0;
    default:throw std::runtime_error("Unrecovered array predicate selector");}
}
int32_t Count(Vm& vm,const Value& array){const auto v=vm.load_reference(array);Need(v.kind==Value::Kind::ArrayHeader,"Array argument/header type");return Number(v);}
}
GameValueArrayResult GameValueArray97(script::Vm& vm,const std::vector<script::Value>& args){
    Need(args.size()==2,"Array97 argc");GameValueArrayResult s;
    const auto rows=Count(vm,args[0]),words=Count(vm,args[1]);Need(words%3==0,"Array97 triple length");
    for(uint32_t t=0;t<uint32_t(words)/3;++t){
        const auto index=Number(Element(vm,args[1],3*t)),op=Number(Element(vm,args[1],3*t+1));
        Need(op>=0&&op<=16,"Array97 action selector");if(op==16)continue;
        for(uint32_t row=0;row<uint32_t(rows);++row){const auto object=Element(vm,args[0],row);Action(vm,object,index,op,args[1],3*t+2,s);}
    }
    s.rows=uint32_t(rows);return s; // source status0,not rows selected
}
GameValueArrayResult GameValueArray98(script::Vm& vm,const std::vector<script::Value>& args){
    Need(args.size()==3,"Array98 argc");GameValueArrayResult s;
    const auto rows=Count(vm,args[0]),conditions=Count(vm,args[1]),actions=Count(vm,args[2]);
    Need(conditions%3==0&&actions%3==0,"Array98 triple lengths");
    for(uint32_t row=0;row<uint32_t(rows);++row){
        const auto object=Element(vm,args[0],row);(void)Count(vm,object);++s.rows;bool matches=false;
        for(uint32_t t=0;t<uint32_t(conditions)/3;++t){
            const auto index=Number(Element(vm,args[1],3*t)),op=Number(Element(vm,args[1],3*t+1));
            matches=Predicate(vm,object,index,op,args[1],3*t+2,s);if(!matches)break;
        }
        if(!matches)continue;
        for(uint32_t t=0;t<uint32_t(actions)/3;++t){
            const auto index=Number(Element(vm,args[2],3*t)),op=Number(Element(vm,args[2],3*t+1));
            Action(vm,object,index,op,args[2],3*t+2,s);
        }
        ++s.selected;
    }
    return s;
}
GameValueArrayResult GameValueArray96(script::Vm& vm,const std::vector<script::Value>& args){
    // Reached6A65C ->79308. Optional output holds zero-based matching row
    // indices,not record copies. Its header remains the original capacity;
    // result is the written count and a full output terminates scanning.
    Need(args.size()==3,"Array96 argc");GameValueArrayResult s;
    const auto rows=Count(vm,args[0]),conditions=Count(vm,args[2]);Need(conditions%3==0,"Array96 predicate triples");
    const bool output=args[1].kind==Value::Kind::Reference;
    const auto capacity=output?Count(vm,args[1]):0;
    for(uint32_t row=0;row<uint32_t(rows);++row){
        const auto object=Element(vm,args[0],row);(void)Count(vm,object);++s.rows;bool matches=false;
        for(uint32_t t=0;t<uint32_t(conditions)/3;++t){
            const auto index=Number(Element(vm,args[2],3*t)),op=Number(Element(vm,args[2],3*t+1));
            matches=Predicate(vm,object,index,op,args[2],3*t+2,s);if(!matches)break;
        }
        if(!matches)continue;
        s.indices.push_back(row);
        if(output){Need(s.selected<capacity,"Array96 output capacity exhausted");vm.store_reference(vm.indexed_reference(args[1],uint32_t(s.selected)),Value::integer(int32_t(row)));++s.writes;}
        ++s.selected;if(output&&s.selected>=capacity)break;
    }
    return s;
}
} // namespace dokodemo::recovered
