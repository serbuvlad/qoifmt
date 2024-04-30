QOIFMT
======

Simple [QOI](https://qoiformat.org/) encoder.
[stb_image](https://github.com/nothings/stb/tree/master) is used to
deal with other (input) encodings.

### Usage:

```bash
qoifmt -i input.png -o output.qoi
```

Default input and output are stdin and stdout. `qoifmt` will refuse to use stdin/stdout
if they are connected to a virtual terminal
(see [isatty(3)](https://www.man7.org/linux/man-pages/man3/isatty.3.html)/
[_isatty](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/isatty)).
To suppress this check, pass `-t`. To enable this check for the arguments to `-i`/`-o`,
pass `-T`.


### Exit codes

* `0` - success
* `1` - error on file operations or bad input image
* `2` - tty errror
* `3` - arugment (usage) error

### Building

Build with:

```bash
cc main.c -lm
```

or equivalent.