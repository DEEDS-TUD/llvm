; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=x86_64-- -enable-ipra -print-regusage -o - 2>&1 < %s | FileCheck %s --check-prefix=DEBUG
; RUN: llc -mtriple=x86_64-- -enable-ipra -o - < %s | FileCheck %s

; Here only CL is clobbered so CH should not be clobbred, but CX, ECX and RCX
; should be clobbered.
; DEBUG: main Clobbered Registers: $ah $al $ax $cl $cx $eax $ecx $eflags $hax $rax $rcx

define i8 @main(i8 %X) {
; CHECK-LABEL: main:
; CHECK:       # %bb.0:
; CHECK-NEXT:    movl %edi, %eax
; CHECK-NEXT:    movb $5, %cl
; CHECK-NEXT:    # kill: def $al killed $al killed $eax
; CHECK-NEXT:    mulb %cl
; CHECK-NEXT:    addb $5, %al
; CHECK-NEXT:    retq
  %inc = add i8 %X, 1
  %inc2 = mul i8 %inc, 5
  ret i8 %inc2
}

