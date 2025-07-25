; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 5
; RUN: opt -passes=gvn -S < %s | FileCheck %s

define void @test(ptr %p, i64 %v) {
; CHECK-LABEL: define void @test(
; CHECK-SAME: ptr [[P:%.*]], i64 [[V:%.*]]) {
; CHECK-NEXT:  [[ENTRY:.*:]]
; CHECK-NEXT:    [[TR:%.*]] = trunc nuw i64 [[V]] to i1
; CHECK-NEXT:    br i1 [[TR]], label %[[RET:.*]], label %[[STORE:.*]]
; CHECK:       [[STORE]]:
; CHECK-NEXT:    store i64 0, ptr [[P]], align 4
; CHECK-NEXT:    ret void
; CHECK:       [[RET]]:
; CHECK-NEXT:    store i64 1, ptr [[P]], align 4
; CHECK-NEXT:    ret void
;
entry:
  %tr = trunc nuw i64 %v to i1
  br i1 %tr, label %ret, label %store

store:
  store i64 %v, ptr %p
  ret void

ret:
  store i64 %v, ptr %p
  ret void
}
