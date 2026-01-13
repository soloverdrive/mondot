# Mondot — Mondot Compiler & VM

This programming language was designed to have a relatively simple syntax, with unit-based code organization.

It's still under development, may contain security flaws and other issues.

You can see the language specifications at [mondot-syntax](https://github.com/soloverdrive/mondot-syntax).

## Build

```bash
make
```

## Run

```bash
# run a source file
./mondot ./examples/overloads.mon

# build bytecode
./mondot build input.mon -o output.mdotc

# run bytecode
./mondot run output.mdotc
```

## Quick syntax

* Top-level units: `unit <name> { ... }`
* Functions: `on <return-type> <name>(params) ... end`
* Primitive types: `number`, `string`, `bool`, `array`, `table`

Example:

```mon
unit demo {
  on string echo(s: string) print(s) end

  on void main()
    echo("hello")
  end
}
```

## License

MIT License.  
Copyright (c) 2026 solever.  
MIT License. See [LICENSE](LICENSE) for details.
