# Cómo los datos del juego entran en las ecuaciones y cómo las ecuaciones vuelven al juego

Este documento describe el mes económico de la partida, el que pinta la ventana **Seguimiento**. Cada mes el mapa se mide, las ecuaciones convierten esa medición en tasas y precios, y una parte de ese resultado cambia la partida del mes siguiente. Las cifras del 17 de junio y del 2 de julio de 1956 son las que el juego publicó en esa ventana.

El cierre ocurre en `MacroEconomyMonthly` (`src/macro_economy.cpp`), una vez por mes de `TimerGameEconomy`, y solo si `economy.islm_enabled` está activo. El mismo mes, justo después, `CompaniesPayInterest` cobra a las compañías con la tasa que acaba de salir.

## 1. El mes, en orden

1. **El mapa acumula hechos durante el mes.** Cada entrega aceptada llama a `MacroNoteCargoDelivery`. Cada subsidio de ruta llama a `MacroNoteSubsidyPayment`. La construcción, los vehículos nuevos y el mantenimiento quedan en los libros de las compañías.
2. **Al cambiar el mes se lee el mapa.** Población de los pueblos, plazas de los vehículos, préstamos de las compañías, gasto de capital y coste de mantener la infraestructura.
3. **Se arman las cuentas.** `EconomyLabCloseMonth` pasa esas libras a libras de 1920 y separa consumo, inversión, consumo intermedio, subsidios y PIB.
4. **Se calculan las tasas.** La demanda de bienes, con el consumo de asalariados y de accionistas, entra en Phillips, Taylor y Okun.
5. **Se reparte el ingreso y se coloca el ahorro.** El PIB se parte en salarios y beneficios. Los dividendos salen del beneficio después del interés. Los asalariados consumen con una propensión alta y los accionistas con una baja. El ahorro entra en los depósitos. La demanda de dinero se queda con su parte, los bonos públicos se compran con esos depósitos y el resto queda para préstamos. Lo que se quiso colocar fuera y no cabía en la cuenta corriente empuja el dólar del mes siguiente.
6. **Esos resultados se escriben en la partida.** Precios de construcción, tarifas de carga, interés de los préstamos, caja de las compañías, cantidad de carga que generan pueblos e industrias, y, en enero, la indexación de algunos ajustes.

```text
entregas, obras, pueblos, vehículos, préstamos
        │
        ▼
   cuentas del mes          C, I, consumo intermedio, subsidios, PIB, Y*
        │
        ▼
   Phillips, Taylor, Okun   inflación, tasa, desempleo
        │
        ▼
   ingreso y cartera        salarios, dividendos, depósitos, bonos, tipo de cambio, X, M
        │
        ▼
   la partida del mes siguiente
   tarifas, costes, interés cobrado, carga generada, casas
```

## 2. Del juego a las cuentas

La unidad de las cuentas es la libra de 1920. La libra que ve el jugador se deflacta con el índice de pagos del juego (`inflation_payment`, base 65536):

```text
libras de 1920 = libras nominales × 65536 / índice de pagos
```

| Lo que ocurre en el mapa | Dónde se lee | En qué variable cae |
|---|---|---|
| Pasajeros y correo entregados, valorados a la tarifa | `DeliverGoods` → `MacroNoteCargoDelivery` | Consumo |
| El resto de la carga entregada, a la tarifa | el mismo sitio | Consumo intermedio. No se suma al PIB |
| Piezas entregadas, sin valorar | el mismo contador | Volumen físico. No es PIB |
| Subsidio extra de una ruta | `MacroNoteSubsidyPayment` | Subsidios. Se restan del PIB. No son compras públicas |
| Construcción y vehículos nuevos del año, en flujo mensual | libros `Construction` y `NewVehicles` | Inversión |
| Mantenimiento de vías, carreteras, señales, canales, estaciones y aeropuertos | costes de infraestructura del mes | Se resta del valor del transporte para obtener el PIB |
| Puntos de compras públicas del ajuste | `economy.islm_government_spending` | Compras públicas, en libras |
| Tipo impositivo y puntos de impuesto autónomo | ajustes de la partida | Impuestos |
| Población de los pueblos | suma de `town->cache.population` | Población, y el factor trabajo del potencial |
| Plazas instaladas en los vehículos | suma de `vehicle->cargo_cap` | Capacidad, y el factor logístico del potencial |
| Préstamos vivos de las compañías | `company->current_loan` | Préstamos bancarios y base de la tasa de interés cobrada |
| Puntos de dinero del ajuste | `economy.islm_money_supply` | Dinero nominal. Entra como dato, no se calcula |

El PIB de producción es el valor del transporte menos el mantenimiento y menos los subsidios de ruta:

```text
PIB = (consumo + consumo intermedio) − mantenimiento − subsidios
```

El primer mes con PIB positivo fija la base. El potencial de los meses siguientes no se vuelve a copiar del PIB. Se mueve solo con el capital, la población y la capacidad, cada uno respecto de su base y acotado entre la mitad y el doble:

```text
Y* / Y0 = 1 + 0,30 (K/K0 − 1) + 0,70 (L/L0 − 1) + 0,20 (Logística/Logística0 − 1)
```

Mil puntos de un ajuste valen el PIB de esa base. Compras públicas, impuesto autónomo y la escala del dinero usan esa regla. El tipo impositivo es un porcentaje del PIB y no pasa por los puntos.

La identidad del gasto se cierra dejando la diferencia a la vista:

```text
discrepancia = PIB − (consumo + inversión + compras + exportaciones − importaciones)
```

Esa discrepancia se guarda y se muestra. No se reparte entre las otras partidas para forzar un cero.

## 3. De las cuentas a las tasas

La demanda de bienes de este mes, antes de Phillips y de Taylor, es:

```text
demanda = consumo + inversión + compras + exportaciones − importaciones
        + impulso del dinero real
        + efecto de la tasa
```

El impulso del dinero es el hueco entre el dinero real y una demanda de dinero que crece con el PIB. El ancla es la oferta de dinero del primer mes. Un 10% más de dinero real, con precios quietos, suma un 10% del potencial a la demanda, con un tope de ±20%. El efecto de la tasa usa la sensibilidad de la inversión: por encima del 4% la demanda baja, y por debajo sube, también con tope.

La brecha que ven Phillips, Taylor, Okun y la carga del mes siguiente es esa demanda frente al potencial, no el PIB medido:

```text
brecha = (demanda − Y*) / Y* × 10000
```

El PIB de producción sigue siendo el valor agregado medido.

La brecha que muestra Seguimiento es esa brecha de demanda, en centésimas de punto porcentual. Phillips, Taylor, Okun y la carga del mes siguiente no ven el tramo que se sale de ±20 puntos. Ven la brecha de ciclo, recortada a ±2000.

**Phillips.** La inflación del mes es la inflación esperada más la pendiente por la brecha de ciclo. La pendiente por defecto es 50: medio punto de inflación por cada punto de brecha.

```text
inflación = inflación esperada + pendiente × brecha de ciclo / 100
```

**Taylor.** La tasa neutra usa una tasa real del 2 % y un objetivo de inflación del 2 %. Cada punto de inflación por encima del objetivo suma 1,50 puntos a la tasa. Cada punto de brecha de ciclo suma 0,50. El resultado se recorta entre 0 % y 25 %.

```text
tasa = 2 % + 2 % + 1,50 (inflación − 2 %) + 0,50 × brecha de ciclo
```

**Okun.** Con brecha cero el desempleo es 5 %. Un punto de auge de ciclo quita medio punto de desempleo. También se recorta, entre 0 % y 50 %.

```text
desempleo = 5 % − brecha de ciclo / 2
```

**Dinero real.** El ajuste de dinero son puntos de política. El dinero real son esos puntos deflactados por el índice de precios respecto de la base de calibración. No sustituye a la caja de las compañías.

```text
dinero real = puntos de dinero × índice base / índice actual
```

**Expectativas y precios.** La expectativa se acerca a la inflación realizada según el porcentaje de adaptación de la partida (por defecto, 40). El índice de precios, el índice de costes y el índice de tarifas avanzan un mes de esa inflación anual:

```text
índice nuevo = índice × (1 + inflación anual / 12)
```

La parte de la inflación que pasa a las tarifas es `economy.islm_payment_inflation_percent` (por defecto, el 100 %).

## 4. Del resto del mundo al dólar

El panel **Resto del mundo** está en **Subsidios → Política monetaria y fiscal → Resto del mundo**. Ahí se editan dos ajustes:

- el crecimiento del PIB del resto del mundo, 250 centésimas de punto, es decir 2,50% anual;
- la tasa de interés internacional, 200 centésimas, es decir 2,00% anual.

Hay un solo tipo de cambio, el dólar US, en pesos por dólar. 10000 es 100. Un número mayor es una depreciación del peso.

```text
exportaciones = base × (PIB mundial / 100) × dólar real × términos de intercambio
importaciones = base × (PIB local / PIB de calibración) / dólar real / términos de intercambio
```

El PIB mundial arranca en 100 y crece cada mes a la tasa del panel, sin perder la fracción de un mes a otro. Los términos de intercambio tienen una tendencia plana en 100 y una onda de ±5% que dura un año. La depreciación esperada es el déficit de cuenta corriente más la presión del mes pasado: lo que los hogares quisieron colocar en bonos externos y no cabía en esa cuenta. Esa presión se guarda y mueve el dólar del mes siguiente. Los bonos externos pagan la tasa internacional. Su oferta no tiene cupo: el déficit aumenta el stock y el superávit lo reduce. La posición neta con el exterior es menos ese stock.

La curva IS del gráfico es esta demanda a cada tasa. La curva LM es la tasa que iguala la demanda de dinero al dinero real: sube con el PIB y baja con M/P. La OA-DA usa la misma demanda: a un índice de precios más alto, el dinero real y el dólar real bajan, y la demanda cae.

## 5. De la tasa al tipo de cambio, dentro de esa cuenta exterior

Con la tasa ya calculada, `EconomyLabApplyOpenEconomy` mueve el tipo de cambio. La tasa mundial es la del panel, 2 % si nadie la tocó. La prima es 1 % más la deuda pública sobre el PIB, más un recargo si los hogares no compran todo el déficit. El diferencial, en puntos básicos, se multiplica por 2 y se recorta a ±80 puntos del índice. Un diferencial positivo, tasa interna mayor que la mundial más la prima, abarata la divisa: el tipo queda por debajo del fundamental.

```text
ajuste = recorte( 2 × (tasa de política − tasa mundial − prima) , ±8000 )
tipo de cambio = fundamental × (10000 − ajuste) / 10000
```

Overshooting vale 1 solo cuando el tipo de cambio supera al fundamental.

El fundamental camina hacia la paridad del índice de precios interno respecto de su base, un veinticuatroavo de la distancia cada mes. Las exportaciones crecen con el tipo de cambio real. Las importaciones crecen con la actividad y caen cuando el tipo de cambio real sube. Esas dos partidas entran en la discrepancia del mes. No cambian por sí solas la tarifa que cobra un vehículo.

La tasa de préstamo es la tasa de política más la prima, recortada entre 0 % y 25 %. Esa es la tasa que se guarda en `macro_loan_rate_bp` y la que pagan las compañías. La prima incluye el déficit que los hogares no compraron. El interés de esa tasa se resta del beneficio antes de repartir dividendos.

## 6. De las ecuaciones otra vez al mapa

Estos son los resultados que cambian datos con los que se juega. El resto se guarda para Seguimiento, las gráficas y la partida guardada.

| Resultado de la ecuación | Dato del juego que cambia | Efecto en la partida |
|---|---|---|
| Tasa de préstamo | `macro_loan_rate_bp` y, en puntos porcentuales enteros, `interest_rate` | El mismo mes, `CompaniesPayInterest` y el tope de préstamo usan Taylor más la prima. También se cobra sobre la caja negativa |
| Inflación de Phillips | `inflation_prices` | `RecomputePrices` recalcula el coste de construir, de mantener y de comprar vehículos, y el tope de préstamo |
| Inflación que pasa a las tarifas | `inflation_payment` y `current_payment` de cada carga | La siguiente entrega paga una tarifa distinta. Esa tarifa será el consumo o el consumo intermedio del mes que viene |
| Brecha de ciclo y `islm_output_gap_cargo_link` (por defecto, 50 %) | `MacroActivityPermyriad` | `ScaleByCargoScale` sube o baja la carga que producen pueblos e industrias. El factor queda entre la mitad y el doble |
| Pueblo en crecimiento | `MacroTownIncentivePermyriad` | La casa produce más carga. Con el modelo activo, el contador `grow_counter` baja más deprisa, así que la siguiente casa se construye antes |
| Inversión medida | `macro_capital` | El stock sube con la inversión del mes y pierde 1/600 cada mes, cerca de un 2 % al año. Ese stock mueve el potencial del mes siguiente |
| Enero, con el modelo ya anclado | compras públicas, consumo autónomo, inversión autónoma e impuesto autónomo | Esos cuatro ajustes crecen con la inflación del año. El tipo impositivo y el dinero no se indexan |
| Gasto público, impuestos, comercio y dividendos | caja de las compañías, con un tope del 20 % de la caja positiva | El neto entra o sale por `SubtractMoneyFromCompany`. Los dividendos restan. No se crea carga nueva en el mapa |
| Desempleo de Okun | trabajo efectivo de E-027 | Al 5 % el trabajo es la población. Más desempleo baja el potencial del mes siguiente |

La carga generada en el mes nuevo vuelve a entrar por la primera columna: se entrega, se valora y se convierte en consumo o en consumo intermedio. El interés cobrado sale de la caja de la compañía y, si la compañía pide otro préstamo, cambia el stock de préstamos del mes siguiente. El coste de construir cambia cuánto pueden ampliar la red.

## 7. Lo que las ecuaciones no reescriben

- El PIB no sustituye al beneficio de la compañía. El beneficio del juego sigue siendo tarifa menos costes. Los dividendos, el gasto público, los impuestos y el comercio sí mueven la caja, con el tope del mes.
- El dinero del ajuste no lo fija el banco ni el tipo de cambio. Lo cambia quien mueve **M +10%** o el ajuste `economy.islm_money_supply`. El botón suma o resta el 10 % del valor actual, con un paso mínimo de 1.
- El potencial no se recentra en el PIB cuando la brecha es grande. Si la brecha sale del rango local, Seguimiento lo cuenta como error de calibración.
- El tipo de cambio no se multiplica sobre la tarifa. Llega a la tarifa solo a través de la inflación que Phillips escribe en el índice de pagos.
- Las exportaciones y las importaciones calculadas no crean carga en el mapa. Entran en la demanda, en la discrepancia y en la caja de las compañías.
- Los depósitos reciben el ahorro de los hogares. Los bonos públicos que compran salen de esos depósitos. Los bonos privados quedan dentro de los depósitos, que son los que financian los préstamos. Los bonos externos no tienen un segundo stock: la posición neta es la deuda externa, y el deseo de comprar más empuja la depreciación esperada del mes siguiente.

## 8. El mes del 17 de junio al 2 de julio de 1956, antes de cerrar el circuito

Estas cifras son la lectura de Seguimiento en esa fecha, con la brecha calculada todavía sobre el PIB medido. A partir del circuito de este documento, la brecha y la tasa de un mes nuevo salen de la demanda, así que un **M +10%** posterior mueve la demanda ese mismo mes.

La partida publicada en Seguimiento cruzó un mes económico. El botón **M +10%** pasó el dinero de 121 a 133 puntos. El cierre del mes dejó estos resultados. Las tasas están en el formato de la ventana.

| | 17 de junio | 2 de julio |
|---|---:|---:|
| PIB | 90.823 | 61.549 |
| Potencial | 58.079 | 58.417 |
| Brecha que se muestra | 56,37 % | 5,36 % |
| Brecha que ven Phillips y Taylor | 20,00 % (el tope) | 5,36 % |
| Inflación | 20,90 % | 14,16 % |
| Inflación esperada | 11,48 % | 11,63 % |
| Tasa de política | 25,00 % | 24,92 % |
| Tipo de cambio | 56,82 | 57,23 |
| Fundamental | 101,47 | 101,91 |
| Overshooting | 0 | 0 |
| Dinero nominal | 121 | 133 |

La tasa de junio está en el tope del 25 %. Con una brecha de ciclo del 20 % y una inflación del 20,90 %, Taylor da más del 25 % y el recorte lo deja en 25,00 %. En julio la brecha ya cabe en la banda. Con la inflación del 14,16 %:

```text
tasa = 4 % + 1,50 × (14,16 % − 2 %) + 0,50 × 5,36 %
     = 4 % + 18,24 % + 2,68 %
     = 24,92 %
```

Es la tasa que publicó el juego. La inflación de julio, con la pendiente por defecto de 50 y la expectativa de junio, es la misma cuenta:

```text
14,16 % = 11,48 % + 0,50 × 5,36 puntos de brecha
```

El tipo de cambio de esa fecha usaba esa tasa, una tasa mundial fija del 2 % y una prima del 1 % porque la deuda pública era cero. El panel del resto del mundo, con su tasa internacional editable, es posterior a esta lectura:

```text
junio:  ajuste = 2 × (25,00 − 2,00 − 1,00) = 44,00
        101,47 × (1 − 0,4400) = 56,82

julio:  ajuste = 2 × (24,92 − 2,00 − 1,00) = 43,84
        101,91 × (1 − 0,4384) = 57,23
```

El tipo queda por debajo del fundamental, así que Overshooting sigue en 0. En esa lectura la subida de 12 puntos de dinero no entraba todavía en la demanda. Con el circuito cerrado, el mismo golpe entra en la demanda de ese mes y la tasa de Taylor responde a la brecha nueva.

El PIB bajó de 90.823 a 61.549 en ese mismo cierre. Esa cifra es la medición del transporte del mes, no una producción que la ecuación IS imponga. La brecha pasó del 56,37 % al 5,36 % porque el PIB medido se acercó al potencial. El potencial casi no se movió: 58.079 a 58.417.

## 9. Dónde se ve en la partida

**Subsidios → Política monetaria y fiscal** muestra los ajustes que entran como datos: compras, impuestos, dinero y subsidios. **M +10%** cambia el dinero y anota el golpe.

**Resto del mundo**, en esa misma ventana, muestra el dólar US, el PIB mundial, las exportaciones, las importaciones, los términos de intercambio, la depreciación esperada y los bonos externos. Los botones **g*** e **i*** mueven el crecimiento mundial y la tasa internacional un 10 % de su valor.

**Panel macro → Seguimiento** muestra el resultado del mes: PIB, demanda de bienes, potencial, brecha, tasas, dólar, Overshooting, precios y dinero. Las gráficas IS-LM y OA-DA leen esa demanda.

Los botones **x1**, **x10**, **x100** y **x1000** del panel solo cambian cuántos meses caben en un minuto. No cambian las ecuaciones.
