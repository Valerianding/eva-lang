; ModuleID = 'EvaLLVM'
source_filename = "EvaLLVM"
target triple = "arm64-apple-macosx12.0.0"

%Point_vTable = type { i32 (%Point*)*, i32 (%Point*, i32, i32)* }
%Point = type { %Point_vTable*, i32, i32 }
%Point3D_vTable = type { i32 (%Point3D*)*, i32 (%Point3D*, i32, i32, i32)* }
%Point3D = type { %Point3D_vTable*, i32, i32, i32 }

@VERSION = constant i32 42, align 4
@Point_vTable = constant %Point_vTable { i32 (%Point*)* @Point_calc, i32 (%Point*, i32, i32)* @Point_constructor }, align 4
@0 = private unnamed_addr constant [20 x i8] c"Point.calc called!\0A\00", align 1
@Point3D_vTable = constant %Point3D_vTable { i32 (%Point3D*)* @Point3D_calc, i32 (%Point3D*, i32, i32, i32)* @Point3D_constructor }, align 4
@1 = private unnamed_addr constant [15 x i8] c"Point3D.calc!\0A\00", align 1
@2 = private unnamed_addr constant [10 x i8] c"p.x = %d\0A\00", align 1
@GlobalVar = external global %Point

declare i32 @printf(i8*, ...)

declare i8* @malloc(i64)

define i32 @main() {
entry:
  %p1 = call i8* @malloc(i64 16)
  %0 = bitcast i8* %p1 to %Point*
  %1 = getelementptr inbounds %Point, %Point* %0, i32 0, i32 0
  store %Point_vTable* @Point_vTable, %Point_vTable** %1, align 8
  %2 = call i32 @Point_constructor(%Point* %0, i32 10, i32 20)
  %p2 = call i8* @malloc(i64 24)
  %3 = bitcast i8* %p2 to %Point3D*
  %4 = getelementptr inbounds %Point3D, %Point3D* %3, i32 0, i32 0
  store %Point3D_vTable* @Point3D_vTable, %Point3D_vTable** %4, align 8
  %5 = call i32 @Point3D_constructor(%Point3D* %3, i32 10, i32 20, i32 30)
  %6 = getelementptr inbounds %Point3D, %Point3D* %3, i32 0, i32 3
  %7 = load i32, i32* %6, align 4
  %8 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([10 x i8], [10 x i8]* @2, i32 0, i32 0), i32 %7)
  %9 = call i32 @check(%Point* %0)
  %10 = bitcast %Point3D* %3 to %Point*
  %11 = call i32 @check(%Point* %10)
  ret i32 0
}

define i32 @Point_constructor(%Point* %0, i32 %1, i32 %2) {
entry:
  %3 = alloca %Point*, align 8
  store %Point* %0, %Point** %3, align 8
  %4 = alloca i32, align 4
  store i32 %1, i32* %4, align 4
  %5 = alloca i32, align 4
  store i32 %2, i32* %5, align 4
  %6 = load i32, i32* %4, align 4
  %7 = load %Point*, %Point** %3, align 8
  %8 = getelementptr inbounds %Point, %Point* %7, i32 0, i32 1
  store i32 %6, i32* %8, align 4
  %9 = load i32, i32* %5, align 4
  %10 = load %Point*, %Point** %3, align 8
  %11 = getelementptr inbounds %Point, %Point* %10, i32 0, i32 2
  store i32 %9, i32* %11, align 4
  ret i32 %9
}

define i32 @Point_calc(%Point* %0) {
entry:
  %1 = alloca %Point*, align 8
  store %Point* %0, %Point** %1, align 8
  %2 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([20 x i8], [20 x i8]* @0, i32 0, i32 0))
  %3 = load %Point*, %Point** %1, align 8
  %4 = getelementptr inbounds %Point, %Point* %3, i32 0, i32 1
  %5 = load i32, i32* %4, align 4
  %6 = load %Point*, %Point** %1, align 8
  %7 = getelementptr inbounds %Point, %Point* %6, i32 0, i32 2
  %8 = load i32, i32* %7, align 4
  %9 = add i32 %5, %8
  ret i32 %9
}

define i32 @Point3D_constructor(%Point3D* %0, i32 %1, i32 %2, i32 %3) {
entry:
  %4 = alloca %Point3D*, align 8
  store %Point3D* %0, %Point3D** %4, align 8
  %5 = alloca i32, align 4
  store i32 %1, i32* %5, align 4
  %6 = alloca i32, align 4
  store i32 %2, i32* %6, align 4
  %7 = alloca i32, align 4
  store i32 %3, i32* %7, align 4
  %8 = load i32 (%Point*, i32, i32)*, i32 (%Point*, i32, i32)** getelementptr inbounds (%Point_vTable, %Point_vTable* @Point_vTable, i32 0, i32 1), align 8
  %9 = load %Point3D*, %Point3D** %4, align 8
  %10 = bitcast %Point3D* %9 to %Point*
  %11 = load i32, i32* %5, align 4
  %12 = load i32, i32* %6, align 4
  %13 = call i32 %8(%Point* %10, i32 %11, i32 %12)
  %14 = load i32, i32* %7, align 4
  %15 = load %Point3D*, %Point3D** %4, align 8
  %16 = getelementptr inbounds %Point3D, %Point3D* %15, i32 0, i32 3
  store i32 %14, i32* %16, align 4
  ret i32 %14
}

define i32 @Point3D_calc(%Point3D* %0) {
entry:
  %1 = alloca %Point3D*, align 8
  store %Point3D* %0, %Point3D** %1, align 8
  %2 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @1, i32 0, i32 0))
  %3 = load i32 (%Point*)*, i32 (%Point*)** getelementptr inbounds (%Point_vTable, %Point_vTable* @Point_vTable, i32 0, i32 0), align 8
  %4 = load %Point3D*, %Point3D** %1, align 8
  %5 = bitcast %Point3D* %4 to %Point*
  %6 = call i32 %3(%Point* %5)
  %7 = load %Point3D*, %Point3D** %1, align 8
  %8 = getelementptr inbounds %Point3D, %Point3D* %7, i32 0, i32 3
  %9 = load i32, i32* %8, align 4
  %10 = add i32 %6, %9
  ret i32 %10
}

define i32 @check(%Point* %0) {
entry:
  %1 = alloca %Point*, align 8
  store %Point* %0, %Point** %1, align 8
  %2 = load %Point*, %Point** %1, align 8
  %3 = getelementptr inbounds %Point, %Point* %2, i32 0, i32 0
  %vt = load %Point_vTable*, %Point_vTable** %3, align 8
  %4 = getelementptr inbounds %Point_vTable, %Point_vTable* %vt, i32 0, i32 0
  %5 = load i32 (%Point*)*, i32 (%Point*)** %4, align 8
  %6 = load %Point*, %Point** %1, align 8
  %7 = call i32 %5(%Point* %6)
  ret i32 %7
}
