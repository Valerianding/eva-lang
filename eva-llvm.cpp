#include <iostream>
#include <string>
#include "src/EvaLLVM.h"
int main() {
    std::string program = R"(
        (class Point null
            (begin
                (var x 0)
                (var y 0)
                (def constructor (self x y)
                    (begin
                        (set (prop self x) x)
                        (set (prop self y) y)
                    )
                )
                (def calc (self)
                    (begin
                        (printf "Point.calc called!\n")
                        (+ (prop self x)(prop self y))
                    )
                )
            )
        )
        (class Point3D Point
            (begin
                (var z 100)

                (def constructor (self x y z)
                    (begin
                        ((method (super Point3D) constructor) self x y)
                        (set (prop self z) z)))
                (def calc (self)
                    (begin
                        (printf "Point3D.calc!\n")
                        (+ ((method (super Point3D) calc) self) (prop self z))
                    )
                )
            )
        )
        (var p1 (new Point 10 20))
        (var p2 (new Point3D 10 20 30))
        (printf "p.x = %d\n" (prop p2 z))

        (def check ((obj Point))
            (begin
                ((method obj calc) obj)
            )
        )
        (check p1)
        (check p2)
    )";

    EvaLLVM vm;
    vm.exec(program);
}

