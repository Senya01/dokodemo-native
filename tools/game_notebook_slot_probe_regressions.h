#pragma once
// Synthetic public component. No private GAME11 issuer or actual preview credit.
#include "game_notebook_regressions.h"
inline void GameNotebookSlotProbeRegressionChecks(){
    using namespace dokodemo;using namespace notebook_tests;
    const auto check=[](bool ok,unsigned n){if(!ok)throw std::runtime_error("NOTE slot probe P"+std::to_string(n));};
    auto program=Program();script::Vm vm(program);check(vm.start().status==script::Status::Running,1);(void)vm.context_value();
    const auto root=Ref(T::Global,4),zero=Ref(T::Global,32),one=Ref(T::Global,64);
    vm.store_reference(root,Header(2));vm.store_reference(Ref(T::Global,5),zero);vm.store_reference(Ref(T::Global,6),one);
    vm.store_reference(zero,Header(3));vm.store_reference(Ref(T::Global,33),V::integer(2));vm.store_reference(Ref(T::Global,34),Text("Bob"));
    vm.store_reference(Ref(T::Global,35),V::integer(7));vm.store_reference(one,Header(1));vm.store_reference(Ref(T::Global,65),Nil());
    unsigned accesses=0;vm.access_observer([&](const V&,bool){++accesses;});
    const auto before=vm.current_witness();const auto p=GameObserveNotebookSlot(vm,71,root,0);const auto rows=GameObserveNotebookRows(vm,71,root);
    check(p.valid&&!p.error&&p.cells==3&&p.row_table==U(T::Global)&&p.row_index==32&&p.row_header==0x90000003,2);
    check(p.first[0].kind==V::Kind::Scalar&&p.first[0].word==0x20000002&&p.first[1].kind==V::Kind::String&&p.first[1].cp932_bytes==3,3);
    check(rows.valid&&rows.populated==0&&rows.cells==1&&rows.slot0.valid&&rows.slot0.sha==p.sha,4);
    check(!accesses&&SameVm(before,vm.current_witness()),5);
    vm.store_reference(Ref(T::Global,35),V::integer(8));accesses=0;
    const auto changed=GameObserveNotebookRows(vm,71,root);
    check(changed.sha==rows.sha&&changed.slot0.sha!=p.sha&&changed.cells==rows.cells,6); // slot0 remains a separate image
    check(!accesses,7);
    for(const auto bad:{GameObserveNotebookSlot(vm,71,root,2),GameObserveNotebookSlot(vm,0,root,0),GameObserveNotebookSlot(vm,71,Nil(),0)})check(!bad.valid&&bad.error,8);
    vm.store_reference(zero,Header(65));check(!GameObserveNotebookSlot(vm,71,root,0).valid,9);vm.store_reference(zero,Header(3));
    auto frame=Ref(T::Frame,0);vm.store_reference(Ref(T::Global,35),frame);accesses=0;
    check(!GameObserveNotebookSlot(vm,71,root,0).valid,10);
    const auto legacy=GameObserveNotebookRows(vm,71,root);check(legacy.valid&&legacy.sha==rows.sha&&!legacy.slot0.valid&&!accesses,11);
    vm.store_reference(Ref(T::Global,5),Nil());accesses=0;
    check(!GameObserveNotebookSlot(vm,71,root,0).valid&&GameObserveNotebookRows(vm,71,root).valid&&!accesses,12);
}
