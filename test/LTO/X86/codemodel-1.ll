; RUN: llvm-as %s -o %t.o
; RUN: llvm-lto2 run -r %t.o,_start,px %t.o -o %t.s
; RUN: llvm-objdump -d %t.s.0 | FileCheck %s --check-prefix=CHECK-SMALL

target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

!llvm.module.flags = !{!0, !1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"Code Model", i32 1}

@data = internal constant [0 x i32] []

define i32* @_start() nounwind readonly {
entry:
; CHECK-SMALL-LABEL:  _start:
; CHECK-SMALL: leaq    (%rip), %rax
    ret i32* getelementptr ([0 x i32], [0 x i32]* @data, i64 0, i64 0)
}
