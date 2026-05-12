Semantic: Analyzing module gm
Semantic: Inferred return type for length as float
CodeGen: Function vec2_operator+ returns vec2
CodeGen: Function vec2_operator- returns vec2
CodeGen: Function vec2_operator* returns vec2
CodeGen: Function sqrtS returns void
CodeGen: Function sqrt returns float
CodeGen: Function length returns float
[1;36m── IR: gm ──[0m
; ModuleID = 'gm'
source_filename = "gm"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%vec2 = type { double, double }

@0 = private unnamed_addr constant [9 x i8] c"__main__\00", align 1
@__name__ = internal global ptr @0
@__arg__ = internal global { ptr, i64, i64 } zeroinitializer
@g = internal constant double 9.800000e+00
@a = internal global %vec2 { double 1.000000e+01, double 1.000000e+01 }
@b = internal global %vec2 { double 4.500000e+01, double 5.000000e+00 }
@1 = private unnamed_addr constant [73 x i8] c"|x| = %f, |y| = %f, A(x,y) = (%f,%f), B(x,y) = (%f,%f), C= A+B = (%f,%f)\00", align 1
@2 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare i32 @printf(ptr, ...)

define %vec2 @"vec2_operator+"(ptr %0, %vec2 %1, %vec2 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %a = alloca %vec2, align 8
  store %vec2 %1, ptr %a, align 8
  %b = alloca %vec2, align 8
  store %vec2 %2, ptr %b, align 8
  %a1 = load %vec2, ptr %a, align 8
  %3 = extractvalue %vec2 %a1, 0
  %b2 = load %vec2, ptr %b, align 8
  %4 = extractvalue %vec2 %b2, 0
  %5 = fadd double %3, %4
  %6 = insertvalue %vec2 undef, double %5, 0
  %a3 = load %vec2, ptr %a, align 8
  %7 = extractvalue %vec2 %a3, 1
  %b4 = load %vec2, ptr %b, align 8
  %8 = extractvalue %vec2 %b4, 1
  %9 = fadd double %7, %8
  %10 = insertvalue %vec2 %6, double %9, 1
  ret %vec2 %10
}

define %vec2 @vec2_operator-(ptr %0, %vec2 %1, %vec2 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %a = alloca %vec2, align 8
  store %vec2 %1, ptr %a, align 8
  %b = alloca %vec2, align 8
  store %vec2 %2, ptr %b, align 8
  %a1 = load %vec2, ptr %a, align 8
  %3 = extractvalue %vec2 %a1, 0
  %b2 = load %vec2, ptr %b, align 8
  %4 = extractvalue %vec2 %b2, 0
  %5 = fsub double %3, %4
  %6 = insertvalue %vec2 undef, double %5, 0
  %a3 = load %vec2, ptr %a, align 8
  %7 = extractvalue %vec2 %a3, 1
  %b4 = load %vec2, ptr %b, align 8
  %8 = extractvalue %vec2 %b4, 1
  %9 = fsub double %7, %8
  %10 = insertvalue %vec2 %6, double %9, 1
  ret %vec2 %10
}

define %vec2 @"vec2_operator*"(ptr %0, %vec2 %1, %vec2 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %a = alloca %vec2, align 8
  store %vec2 %1, ptr %a, align 8
  %b = alloca %vec2, align 8
  store %vec2 %2, ptr %b, align 8
  %a1 = load %vec2, ptr %a, align 8
  %3 = extractvalue %vec2 %a1, 0
  %b2 = load %vec2, ptr %b, align 8
  %4 = extractvalue %vec2 %b2, 0
  %5 = fmul double %3, %4
  %6 = insertvalue %vec2 undef, double %5, 0
  %a3 = load %vec2, ptr %a, align 8
  %7 = extractvalue %vec2 %a3, 1
  %b4 = load %vec2, ptr %b, align 8
  %8 = extractvalue %vec2 %b4, 1
  %9 = fmul double %7, %8
  %10 = insertvalue %vec2 %6, double %9, 1
  ret %vec2 %10
}

define void @sqrtS(double %0, double %1) {
entry:
  %x_g = alloca double, align 8
  store double %0, ptr %x_g, align 8
  %v = alloca double, align 8
  store double %1, ptr %v, align 8
  %x_g1 = load double, ptr %x_g, align 8
  %x_g2 = load double, ptr %x_g, align 8
  %2 = fmul double %x_g1, %x_g2
  %v3 = load double, ptr %v, align 8
  %3 = fadd double %2, %v3
  %x_g4 = load double, ptr %x_g, align 8
  %4 = fmul double 2.000000e+00, %x_g4
  %5 = fdiv double %3, %4
  %6 = load double, ptr %x_g, align 8
  store double %5, ptr %x_g, align 8
  ret void
}

define double @sqrt(double %0) {
entry:
  %a = alloca double, align 8
  store double %0, ptr %a, align 8
  %x_g = alloca i64, align 8
  store i64 0, ptr %x_g, align 8
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %forcond

forcond:                                          ; preds = %forstep, %entry
  %1 = load i64, ptr %i, align 8
  %2 = icmp slt i64 %1, 100
  br i1 %2, label %forbody, label %forcont

forbody:                                          ; preds = %forcond
  %x_g1 = load i64, ptr %x_g, align 8
  %a2 = load double, ptr %a, align 8
  call void @sqrtS(i64 %x_g1, double %a2)
  br label %forstep

forstep:                                          ; preds = %forbody
  %3 = add i64 %1, 1
  store i64 %3, ptr %i, align 8
  br label %forcond

forcont:                                          ; preds = %forcond
  %x_g3 = load i64, ptr %x_g, align 8
  %4 = sitofp i64 %x_g3 to double
  ret double %4
}

define double @length(%vec2 %0) {
entry:
  %a = alloca %vec2, align 8
  store %vec2 %0, ptr %a, align 8
  %a1 = load %vec2, ptr %a, align 8
  %1 = extractvalue %vec2 %a1, 0
  %a2 = load %vec2, ptr %a, align 8
  %2 = extractvalue %vec2 %a2, 0
  %3 = fmul double %1, %2
  %a3 = load %vec2, ptr %a, align 8
  %4 = extractvalue %vec2 %a3, 1
  %a4 = load %vec2, ptr %a, align 8
  %5 = extractvalue %vec2 %a4, 1
  %6 = fmul double %4, %5
  %7 = fadd double %3, %6
  %8 = call double @sqrt(double %7)
  ret double %8
}

define internal void @__luv_script_init() {
entry:
  %g = alloca double, align 8
  store double 9.800000e+00, ptr %g, align 8
  %a = alloca %vec2, align 8
  store %vec2 { double 1.000000e+01, double 1.000000e+01 }, ptr %a, align 8
  %b = alloca %vec2, align 8
  store %vec2 { double 4.500000e+01, double 5.000000e+00 }, ptr %b, align 8
  %a1 = load %vec2, ptr %a, align 8
  %0 = call double @length(%vec2 %a1)
  %b2 = load %vec2, ptr %b, align 8
  %1 = call double @length(%vec2 %b2)
  %a3 = load %vec2, ptr %a, align 8
  %2 = extractvalue %vec2 %a3, 0
  %a4 = load %vec2, ptr %a, align 8
  %3 = extractvalue %vec2 %a4, 1
  %b5 = load %vec2, ptr %b, align 8
  %4 = extractvalue %vec2 %b5, 0
  %b6 = load %vec2, ptr %b, align 8
  %5 = extractvalue %vec2 %b6, 1
  %a7 = load %vec2, ptr %a, align 8
  %b8 = load %vec2, ptr %b, align 8
  %6 = call %vec2 @"vec2_operator+"(ptr %a, %vec2 %b8)
  %7 = extractvalue %vec2 %6, 0
  %a9 = load %vec2, ptr %a, align 8
  %b10 = load %vec2, ptr %b, align 8
  %8 = call %vec2 @"vec2_operator+"(ptr %a, %vec2 %b10)
  %9 = extractvalue %vec2 %8, 1
  %10 = call i32 (ptr, ...) @printf(ptr @1, double %0, double %1, double %2, double %3, double %4, double %5, double %7, double %9)
  %11 = call i32 (ptr, ...) @printf(ptr @2)
  ret void
}

define i64 @main() {
entry:
  call void @__luv_script_init()
  ret i64 0
}
[1;32mSuccessfully compiled tests/gm.lv to output.o[0m
