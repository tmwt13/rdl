# RDL — Report Definition Language

Eigene, kleine Skriptsprache zur Report-Erzeugung. Wird in eine Report-Vorlage
(Text-Datei) eingebettet: alles außerhalb von `{{ ... }}` wird 1:1 in die
Ausgabedatei kopiert, der Inhalt jedes `{{ ... }}`-Blocks wird als RDL-Programm
ausgewertet und sein Output an derselben Stelle eingefügt.

```
Report:

{{
   set name 'Tom'
}}

{{
   y: 0

   loop Some.iterator(arg0, arg1)
      .title nl
      'Member1 = ' .member1 nl
      set y y+.value
   end
}}
   
Summe von {{ name }} ist {{ y }}.
```
### Context

RDL besitzt einen globalen Context. Auf diesen kann mit `.` zugegriffen werden. Bsp. `Account` Object im Context:
```
.this
.name
.balance
.someMethod(a,b,c)
```
`.this` liefert das Aktuelle Ergebnis im Context.

#### With-Keyword
Ein temporärer Context kann mit `with VALUE BODY end` gesetzt werden:
```
with 9 + 1
    set elf .this + 1
    some_fn(...)
    print(.this)
end
```

### Elemente

- **Literale**: Zahlen (`10`, `3.5`), Text in `'...'` oder `"..."`.
- **Variablen**: `set name wert` weist zu 
- **Function-Calls**: `empfänger.methode(args)`, z. B. `.balance`, `.name`,
  `Ledger.get("assets")`.
- **`loop OBJ ... end`**: iteriert über ein Objekt, das `iteratorStep`/
  `iteratorValue` implementiert (aktuell: `Ledger`, `Account`, `Number`, `Text`).
  Im Rumpf ist das aktuelle Element implizit das Kontextobjekt (`.` bezieht sich
  darauf).
- **`if / else`**, arithmetische (`+ - * /`) und Vergleichsoperatoren
  (`< > <= >= == !`), `and`/`or`.
- **`nl()` / `tab(n)`**: Zeilenumbruch bzw. `n` Tabs in den Output schreiben.


