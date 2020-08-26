# incbin

Include binary files in your C/C++ applications with ease

## Example

``` c
    #include "incbin.h"

    INCBIN(Icon, "icon.png");

    // This translation unit now has three symbols
    // const unsigned char gIconData[];
    // const unsigned char *const gIconEnd; // a marker to the end, take the address to get the ending pointer
    // const unsigned int gIconSize;

    // Reference in other translation units like this
    INCBIN_EXTERN(Icon);

    // This translation unit now has three extern symbols
    // Use `extern "C"` in case of writing C++ code
    // extern const unsigned char gIconData[];
    // extern const unsigned char *const gIconEnd; // a marker to the end, take the address to get the ending pointer
    // extern const unsigned int gIconSize;
```

## Portability

Known to work on the following compilers

* GCC
* Clang
* PathScale
* Intel
* Solaris & Sun Studio
* Green Hills
* SNC (ProDG)
* Diab C++ (WindRiver)
* XCode
* ArmCC
* RealView
* ImageCraft
* Stratus VOS C
* TinyCC
* cparser & libfirm
* LCC
* MSVC _See MSVC below_

If your compiler is not listed, as long as it supports GCC inline assembler, this
should work.

## MISRA
INCBIN can be used in MISRA C setting. However it should be independently checked
due to its use of inline assembly to achieve what it does. Independent verification
of the header has been done several times based on commit: 7e327a28ba5467c4202ec37874beca7084e4b08c

## Alignment

The data included by this tool will be aligned on the architectures word boundary
unless some variant of SIMD is detected, then it's aligned on a byte boundary that
respects SIMD convention just incase your binary data may be used in vectorized
code. The table of the alignments for SIMD this header recognizes is as follows:

| SIMD                                    | Alignment |
|-----------------------------------------|-----------|
| SSE, SSE2, SSE3, SSSE3, SSE4.1, SSE4.2  | 16        |
| Neon                                    | 16        |
| AVX, AVX2                               | 32        |
| AVX512                                  | 64        |

## Prefix
By default, `incbin.h` emits symbols with a `g` prefix. This can be adjusted by
defining `INCBIN_PREFIX` before including `incbin.h` with a desired prefix. For
instance

``` c
    #define INCBIN_PREFIX g_
    #include "incbin.h"
    INCBIN(test, "test.txt");

    // This translation unit now has three symbols
    // const unsigned char g_testData[];
    // const unsigned char *const g_testEnd;
    // const unsigned int g_testSize;
```

You can also choose to have no prefix by defining the prefix with nothing, for example:

``` c
    #define INCBIN_PREFIX
    #include "incbin.h"
    INCBIN(test, "test.txt");

    // This translation unit now has three symbols
    // const unsigned char testData[];
    // const unsigned char *const testEnd;
    // const unsigned int testSize;
```

## Style
By default, `incbin.h` emits symbols with `CamelCase` style. This can be adjusted
by defining `INCBIN_STYLE` before including `incbin.h` to change the style. There
are two possible styles to choose from

* INCBIN_STYLE_CAMEL (CamelCase)
* INCBIN_STYLE_SNAKE (snake_case)

For instance:

``` c
    #define INCBIN_STYLE INCBIN_STYLE_SNAKE
    #include "incbin.h"
    INCBIN(test, "test.txt");

    // This translation unit now has three symbols
    // const unsigned char gtest_data[];
    // const unsigned char *const gtest_end;
    // const unsigned int gtest_size;
```

Combining both the style and prefix allows for you to adjust `incbin.h` to suite
your existing style and practices.

## Overriding Linker Output section
By default, `incbin.h` emits into the read-only linker output section used on
the detected platform. If you need to override this for whatever reason, you
can manually specify the linker output section.

For example, to emit data into program memory for
[esp8266/Arduino](github.com/esp8266/Arduino):

``` c
#define INCBIN_OUTPUT_SECTION ".irom.text"
#include "incbin.h"
INCBIN(Foo, "foo.txt");
// Data is emitted into program memory that never gets copied to RAM
```

## Explanation

`INCBIN` is a macro which uses the inline assembler provided by almost all
compilers to include binary files. It achieves this by utilizing the `.incbin`
directive of the inline assembler. It then uses the assembler to calculate the
size of the included binary and exports two global symbols that can be externally
referenced in other translation units which contain the data and size of the
included binary data respectively.

## MSVC

Supporting MSVC is slightly harder as MSVC lacks an inline assembler which can
include data. To support this we ship a tool which can process source files
containing `INCBIN` macro usage and generate an external source file containing
the data of all of them combined. This file is named `data.c` by default.
Just include it into your build and use the `incbin.h` to reference data as
needed. It's suggested you integrate this tool as part of your projects's
pre-build events so that this can be automated. A more comprehensive list of
options for this tool can be viewed by invoking the tool with `-help`

If you're using a custom prefix, be sure to specify the prefix on the command
line with `-p <prefix>` so that everything matches up; similarly, if you're
using a custom style, be sure to specify the style on the command line with
`-S <style>` as well.

## Miscellaneous

Documentation for the API is provided by the header using Doxygen notation.
For licensing information see UNLICENSE.
