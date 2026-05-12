; ModuleID = 'test_while_simple'
source_filename = "test_while_simple"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@0 = private unnamed_addr constant [9 x i8] c"__main__\00", align 1
@__name__ = internal global ptr @0
@__arg__ = internal global { ptr, i64, i64 } zeroinitializer
@1 = private unnamed_addr constant [5 x i8] c"%lld\00", align 1
@2 = private unnamed_addr constant [7 x i8] c" next \00", align 1
@3 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@4 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare i32 @printf(ptr, ...)

define i64 @main() {
entry:
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %whilecond

whilecond:                                        ; preds = %whilestep, %entry
  %i1 = load i64, ptr %i, align 8
  %0 = icmp slt i64 %i1, 3
  br label %chain_merge2

chain_merge2:                                     ; preds = %whilecond
  %1 = phi i1 [ %0, %whilecond ]
  br i1 %1, label %whilebody, label %whilecont

whilebody:                                        ; preds = %chain_merge2
  %i3 = load i64, ptr %i, align 8
  %2 = call i32 (ptr, ...) @printf(ptr @1, i64 %i3)
  %3 = load i64, ptr %i, align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr %i, align 8
  br label %whilestep

whilestep:                                        ; preds = %whilebody
  %5 = call i32 (ptr, ...) @printf(ptr @2)
  br label %whilecond

whilecont:                                        ; preds = %chain_merge2
  %6 = call i32 (ptr, ...) @printf(ptr @3)
  %7 = call i32 (ptr, ...) @printf(ptr @4)
  ret i64 0
}
