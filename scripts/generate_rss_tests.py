import os

# Categories and their specific patterns
CATEGORIES = {
    "linearity": [
        "fn main() { unique x = 10; unique y = x; unique z = x; 0 } // should error: unique move twice",
        "fn f(unique x) { 0 } fn main() { unique x = 10; f(x); f(x); 0 } // should error: unique used after move",
    ],
    "escape": [
        "fn f() : unique { unique x = 10; return x; } fn main() { f(); 0 } // escape to caller",
    ],
    "aliasing": [
        "fn main() { unique x = 10; lent y = x; lent z = x; 0 } // multiple lent aliases",
        "fn main() { unique x = 10; lent y = x; x = 20; 0 } // mutate unique with active lent",
    ],
    "speculative": [
        "![speculative] fn main() { unique x = 10; if (true) { 0 } 0 }",
    ],
    "degradation": [
        "![tier0] fn main() { unique x = 10; 0 }",
        "![tier3] fn main() { unique x = 10; 0 }",
    ],
    "sao_tag": [
        "fn main() { unique x = 10; rc_shared y = x; 0 } // state transition UNIQUE -> RC_SHARED",
        "fn main() { rc_shared x = 10; unique y = x; 0 } // should error: invalid demotion without isolation",
    ],
    "interprocedural": [
        "fn pure_f(unique x) : unique { return x; } fn main() { unique x = 10; unique y = pure_f(x); 0 }",
    ],
    "psf_stress": [
        "fn main() { for i in 0..100 { rc_shared x = i; } 0 } // trigger PSF",
    ]
}

def generate_tests():
    os.makedirs("tests/rss_suite", exist_ok=True)
    count = 0
    
    # Base tests from categories
    for cat, patterns in CATEGORIES.items():
        for i, pattern in enumerate(patterns):
            with open(f"tests/rss_suite/{cat}_{i}.lv", "w") as f:
                f.write(f"// RSS Test: {cat} {i}\n")
                if "should error" in pattern:
                    f.write("// should error\n")
                f.write(f"{pattern}\n")
            count += 1
            
    # Procedural generation for the rest to reach 200
    while count < 205:
        cat = list(CATEGORIES.keys())[count % len(CATEGORIES)]
        with open(f"tests/rss_suite/gen_{count}.lv", "w") as f:
            f.write(f"// RSS Generated Test {count} (Category: {cat})\n")
            f.write("fn main() {\n")
            f.write(f"    unique x_{count} = {count};\n")
            if count % 3 == 0:
                f.write(f"    unique y_{count} = x_{count};\n")
            elif count % 3 == 1:
                f.write(f"    lent y_{count} = x_{count};\n")
            else:
                f.write(f"    rc_shared y_{count} = x_{count};\n")
            f.write("    0\n")
            f.write("}\n")
        count += 1
        
    print(f"Generated {count} tests in tests/rss_suite/")

if __name__ == "__main__":
    generate_tests()
