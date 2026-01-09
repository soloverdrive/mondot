# Solev — Mondot Compiler & VM

This programming language was designed to have a relatively simple syntax, with unit-based code organization.

It's still under development, may contain security flaws and other issues.

## Build

```bash
make
```

## Run

```bash
# run a source file
./solev ./examples/overloads.mon

# build bytecode
./solev build input.mon -o output.mdotc

# run bytecode
./solev run output.mdotc
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
Copyright (c) 2026 Solever.  
MIT License. See [LICENSE](LICENSE) for details.
