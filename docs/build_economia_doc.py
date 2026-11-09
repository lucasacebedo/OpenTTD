# -*- coding: utf-8 -*-
"""Documento de la economía del juego y plan director. No modifica el modelo."""

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

OUT = r"C:\ProyectosAI\Open TTD 2\docs\Economia-del-juego-plan-director.docx"

NAVY = RGBColor(0x1F, 0x3A, 0x5F)
RULE = RGBColor(0xC5, 0xCD, 0xD6)
HEAD_BG = "1F3A5F"
ZEBRA = "F4F7FA"
EQ_BG = "EEF3F8"


def shade(cell, hex_color):
    tc = cell._tc
    tcPr = tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), hex_color)
    tcPr.append(shd)


def set_run_font(run, name="Arial", size=11, bold=False, italic=False, color=None):
    run.font.name = name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), name)
    run.font.size = Pt(size)
    run.bold = bold
    run.italic = italic
    if color is not None:
        run.font.color.rgb = color


def add_text(p, text, **kwargs):
    run = p.add_run(text)
    set_run_font(run, **kwargs)
    return run


def para(doc, text, size=11, bold=False, italic=False, space_before=0, space_after=8, color=None):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(space_before)
    p.paragraph_format.space_after = Pt(space_after)
    p.paragraph_format.line_spacing = 1.12
    add_text(p, text, size=size, bold=bold, italic=italic, color=color)
    return p


def equation(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(4)
    p.paragraph_format.left_indent = Cm(0.4)
    add_text(p, text, name="Consolas", size=10, color=NAVY)
    return p


def heading(doc, text, level):
    p = doc.add_heading(text, level=level)
    for run in p.runs:
        run.font.color.rgb = NAVY
        run.font.name = "Arial"
    return p


def bullet(doc, text, numbered=False):
    style = "List Number" if numbered else "List Bullet"
    p = doc.add_paragraph(style=style)
    p.paragraph_format.space_after = Pt(3)
    p.clear()
    add_text(p, text, size=11)
    return p


def set_cell_border(cell):
    tc = cell._tc
    tcPr = tc.get_or_add_tcPr()
    tcBorders = OxmlElement("w:tcBorders")
    for edge in ("top", "left", "bottom", "right"):
        el = OxmlElement(f"w:{edge}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), "4")
        el.set(qn("w:color"), "C5CDD6")
        tcBorders.append(el)
    tcPr.append(tcBorders)


def cell_text(cell, text, bold=False, color=None, size=9, fill=None):
    cell.text = ""
    p = cell.paragraphs[0]
    p.paragraph_format.space_before = Pt(2)
    p.paragraph_format.space_after = Pt(2)
    add_text(p, text, size=size, bold=bold, color=color or (RGBColor(255, 255, 255) if fill == HEAD_BG else None))
    set_cell_border(cell)
    if fill:
        shade(cell, fill)
    tc = cell._tc
    tcPr = tc.get_or_add_tcPr()
    mar = OxmlElement("w:tcMar")
    for edge, val in (("top", "40"), ("left", "80"), ("bottom", "40"), ("right", "80")):
        node = OxmlElement(f"w:{edge}")
        node.set(qn("w:w"), val)
        node.set(qn("w:type"), "dxa")
        mar.append(node)
    tcPr.append(mar)


def add_table(doc, headers, rows, widths):
    table = doc.add_table(rows=1 + len(rows), cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    tbl = table._tbl
    tblPr = tbl.tblPr if tbl.tblPr is not None else OxmlElement("w:tblPr")
    tblW = tblPr.find(qn("w:tblW"))
    if tblW is None:
        tblW = OxmlElement("w:tblW")
        tblPr.append(tblW)
    total = sum(widths)
    tblW.set(qn("w:w"), str(int(total * 567)))
    tblW.set(qn("w:type"), "dxa")
    for i, h in enumerate(headers):
        cell_text(table.rows[0].cells[i], h, bold=True, fill=HEAD_BG, size=9)
    for r, row in enumerate(rows):
        fill = ZEBRA if r % 2 == 0 else "FFFFFF"
        for c, value in enumerate(row):
            cell_text(table.rows[r + 1].cells[c], value, size=9, fill=fill)
    for row in table.rows:
        for i, w in enumerate(widths):
            row.cells[i].width = Cm(w)
    doc.add_paragraph().paragraph_format.space_after = Pt(6)
    return table


def footer(section):
    footer = section.footer
    footer.is_linked_to_previous = False
    p = footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    add_text(p, "Economía del juego  ·  plan director  ·  página ", size=9, color=RGBColor(0x5C, 0x6B, 0x7A))
    run = p.add_run()
    set_run_font(run, size=9, color=RGBColor(0x5C, 0x6B, 0x7A))
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = " PAGE "
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    run._r.append(fld_begin)
    run._r.append(instr)
    run._r.append(fld_end)


def build():
    doc = Document()
    section = doc.sections[0]
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.left_margin = Cm(1.8)
    section.right_margin = Cm(1.8)
    section.top_margin = Cm(1.7)
    section.bottom_margin = Cm(1.7)
    section.header_distance = Cm(0.6)
    section.footer_distance = Cm(0.6)
    footer(section)

    normal = doc.styles["Normal"]
    normal.font.name = "Arial"
    normal.font.size = Pt(11)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Arial")

    header = section.header
    header.is_linked_to_previous = False
    hp = header.paragraphs[0]
    hp.alignment = WD_ALIGN_PARAGRAPH.LEFT
    add_text(hp, "OpenTTD  ·  economía cerrada del juego", size=9, color=RGBColor(0x5C, 0x6B, 0x7A))

    para(doc, "ECONOMÍA DEL JUEGO", size=22, bold=True, color=NAVY, space_after=2)
    para(doc, "Qué calcula, cómo se ata a las variables del juego, y plan director para que sea representativa", size=13, italic=True, color=RGBColor(0x3D, 0x4F, 0x63), space_after=4)
    para(doc, "Documento de diagnóstico. Describe el modelo que está compilado hoy. No cambia el código.", size=10, italic=True, space_after=12)

    heading(doc, "1. Para qué sirve este documento", 1)
    para(doc, "La partida que está en pantalla no se comporta como una economía. El producto es 3.199, el potencial es el mismo número, la brecha es cero, el consumo es cero y la tasa de interés está clavada en −50,00%. Ese −50% no es un equilibrio: es el suelo del programa. Por debajo de ese suelo la fórmula pedía cerca de −90%.")
    para(doc, "Este texto hace tres cosas. Primero escribe las ecuaciones que el juego resuelve de verdad, no las que quedaron en un comentario viejo del código. Segundo une cada símbolo con la variable del juego de la que sale. Tercero ordena las inconsistencias como un plan director, para resolverlas en secuencia hasta que la tasa, el consumo y la brecha se muevan dentro de un rango reconocible.")

    heading(doc, "2. Qué es esta economía", 1)
    para(doc, "Es una economía cerrada, de un solo periodo mensual, resuelta con enteros. No hay tipo de cambio ni sector exterior. El mes económico de OpenTTD dispara el cálculo. Las empresas, humanas o de la IA, entran todas: no hace falta una compañía del jugador.")
    para(doc, "La intención de diseño es la de un modelo corto plazo keynesiano, con saldos reales a la Patinkin, y largo plazo neoclásico. En el corto plazo el precio del mes está dado y la demanda determina el gasto. En el largo plazo una curva de Phillips con expectativas adaptativas empuja el producto hacia un potencial que crece con la población, la capacidad de carga y el capital. El desempleo lee esa misma brecha por la ley de Okun.")
    para(doc, "Esa intención ya no es lo que ejecuta el solucionador. El comentario al inicio de macro_economy.cpp todavía dice que el producto sale de la demanda autónoma y que el consumo depende del saldo real. El código que corre hace otra cosa: el producto es la carga entregada, el consumo es el resto después de restar inversión y gasto, y el saldo real solo entra en la tasa. El plan director parte de esa diferencia.")

    heading(doc, "3. Unidades", 1)
    para(doc, "Todo es entero, para que una partida en red no dependa del redondeo de un flotante. Las tasas se guardan en centésimas de punto porcentual: 510 es 5,10% y −5000 es −50,00%. El índice de precios que se ve en el panel es el índice almacenado dividido por 100. Un índice mostrado de 1.109,98 es el entero 110.998.")
    equation(doc, "m  =  M × 1.000 × 100  /  P_almacenado")
    equation(doc, "m  =  2.061 × 100.000  /  110.998  =  1.856")
    para(doc, "Ese 1.856 es el dinero real de la captura. El producto, el consumo, la inversión y el gasto que entran en la identidad están en unidades de carga, no en libras. Las libras del juego (construcción, ingresos, préstamos) se convierten a esa unidad con reglas distintas según el concepto. Esa mezcla de unidades es la raíz de varias inconsistencias del apartado 7.")

    heading(doc, "4. De la partida a cada símbolo", 1)
    para(doc, "La tabla siguiente es el puente. A la izquierda, lo que el jugador reconoce. A la derecha, el símbolo de las ecuaciones.")
    add_table(
        doc,
        ["Variable del juego", "Símbolo", "De dónde sale"],
        [
            ["Carga entregada en el mes", "Y", "Suma de unidades aceptadas en cada entrega. Si el mes aún no acumula, se usa el mes cerrado o el trimestre en curso."],
            ["Producto potencial", "Y*", "Arranca en el producto natural de los ajustes y se acerca a la capacidad de los vehículos, la población y el capital. Si se aleja más de cuatro veces de Y, se iguala a Y."],
            ["Capacidad de carga", "K_carga", "Suma de cargo_cap de todos los vehículos."],
            ["Población y su crecimiento", "N, n", "Suma de la población de los pueblos. n es la variación mensual, en centésimas de punto."],
            ["Capital", "K", "Suma de la inversión ya contada, menos una depreciación de 1/600 al mes."],
            ["Ingresos de las empresas", "R", "Ingresos de tren, carretera, avión y barco en el libro del año. En el juego se guardan como un coste negativo."],
            ["Gasto de capital", "I_libras", "Construcción más vehículos nuevos del año, más el coste de mantener vías, carreteras, señales, canales, estaciones y aeropuertos."],
            ["Compras, subsidio, rutas", "G_ajuste", "Los tres instrumentos de la ventana de política, en puntos, todavía sin pasar a unidades de carga."],
            ["Impuesto autónomo y tipo", "T0, t", "Ajustes. T0 está en puntos. t es un porcentaje."],
            ["Propensión y filtración", "c, s", "Ajustes. c entra en la indexación de enero. s = 1 − c(1−t) se calcula y se guarda, y no determina Y."],
            ["Consumo e inversión autónomos", "C0, I0", "Ajustes. Se indexan con la inflación en enero. No entran en Y ni en C ni en I."],
            ["Dinero nominal", "M", "Ajuste de la ventana de política. No se indexa."],
            ["Índice de precios macro", "P", "Parte de 1.000 y cada mes crece con la inflación de Phillips, un doceavo."],
            ["Inflación del juego", "P_juego", "inflation_prices, la que encarece construir y cobrar. Recibe el mismo paso mensual."],
            ["Deuda pública", "B", "Hasta 48 tramos de bonos. Un déficit emite al tipo del mes. Un superávit recompra el cupón más caro."],
            ["Deuda privada", "D", "Suma de current_loan de todas las compañías, pasada a puntos con 50.000 libras de 1920 por punto."],
            ["Precio de la deuda privada", "q", "Inverso de la tasa, con un suelo de cálculo de 0,50%."],
            ["Préstamo máximo", "L_max", "El máximo del juego, recortado por la parte positiva de la tasa."],
        ],
        [4.6, 2.2, 10.6],
    )

    heading(doc, "5. Ecuaciones que el juego resuelve", 1)
    para(doc, "El orden es el del mes. Primero se miden los flujos del juego. Después se reparte el producto. Después se calcula la tasa. Al cerrar el mes se mueven precios, expectativas, deuda y, en enero, los ajustes autónomos.")

    heading(doc, "5.1 Producto", 2)
    para(doc, "El producto no sale de una demanda agregada. Sale de la carga que las empresas acaban de entregar.")
    equation(doc, "Y  =  suma de unidades de carga entregadas en el periodo")
    para(doc, "Si en este arranque del programa todavía no hay entregas contadas, Y toma el mes ya cerrado o, en su defecto, la carga del trimestre dividida por los meses transcurridos del trimestre. Por eso al reabrir una partida el producto no aparece en cero.")

    heading(doc, "5.2 Inversión", 2)
    para(doc, "La inversión en libras es lo que las empresas gastan en ampliar y en sostener la red. El mantenimiento se calcula aunque la opción de mantenimiento del juego esté apagada: vías, señales, carretera, canales, estaciones y aeropuertos.")
    equation(doc, "I_libras  =  construcción  +  vehículos nuevos  +  mantenimiento de la infraestructura")
    para(doc, "Ese gasto se pasa a unidades de carga comparándolo con el ingreso que produjo. Si hay ingreso, la inversión es la fracción del producto que ese gasto representa frente al ingreso. Si hay gasto y todavía no hay ingreso contabilizado, la inversión es un quinto del producto. Luego se le pone un techo del 45% de Y y un suelo de 1 siempre que el gasto en libras sea positivo.")
    equation(doc, "I  =  Y × I_libras  /  (R + I_libras)     si R > 0")
    equation(doc, "I  =  máx(Y / 5,  1)                     si R = 0 y I_libras > 0")
    equation(doc, "I  =  mín(I,  0,45 Y)                    y, si I_libras > 0,  I ≥ 1")
    para(doc, "La tasa solo recorta esa inversión cuando es positiva. Una tasa negativa no la aumenta.")
    equation(doc, "I  =  máx( 1,   I × 1.000 / (1.000 + b × máx(r, 0)) )")
    para(doc, "b es la sensibilidad de la inversión, en el ajuste islm_investment_sensitivity. Con b = 20 y r = 10%, se conserva el 83% del gasto. Con la tasa de la captura, que es negativa, el recorte es cero.")

    heading(doc, "5.3 Gasto público e impuestos", 2)
    para(doc, "Los puntos de la ventana no son unidades de carga. El programa los estira con el tamaño del producto: mil puntos de política equivalen a un producto entero.")
    equation(doc, "G_ajuste  =  compras  +  subsidio fijado  +  pagos de rutas del mes")
    equation(doc, "G  =  G_ajuste × Y  /  1.000")
    equation(doc, "T  =  T0 × Y  /  1.000   +   t × Y  /  100")
    para(doc, "En la captura, compras 590 y subsidio 420 dan G_ajuste = 1.010. Con Y = 3.199 el gasto pedido es 3.230, mayor que el producto. Los impuestos, con T0 = 100 y t = 5%, son 319 + 159 = 478. El saldo G − T que muestra el panel usa el gasto ya recortado, no este gasto pedido. Por eso el panel dice 1.760 − 478 = 1.282, y no 3.230 − 478.")

    heading(doc, "5.4 Identidad y consumo", 2)
    para(doc, "Después del recorte, si G más I pasan de Y, G se baja hasta dejar sitio a I. El consumo es el residuo. Si la inversión llega a cero, el consumo se fuerza a cero.")
    equation(doc, "si  G > Y − I,    G  =  Y − I")
    equation(doc, "C  =  Y − I − G     si I > 0")
    equation(doc, "C  =  0             si I = 0")
    para(doc, "La identidad C + I + G = Y se cumple por construcción. No es el resultado de una propensión a consumir. En la captura I está en el techo del 45% (1.439) y G se queda con el resto (1.760). El consumo es cero aunque las empresas estén ingresando dinero.")

    heading(doc, "5.5 Mercado de dinero y tasa", 2)
    para(doc, "La demanda de dinero que se dibuja es k por mil del producto. Con el ajuste k = 50, eso es el 5% de Y. La tasa, en centésimas de punto, compara ese 5% con el dinero real medido como fracción del producto.")
    equation(doc, "L  =  k × Y  /  1.000")
    equation(doc, "r_LM  =  (k × 10  −  m × 10.000 / Y)  ×  100  /  h")
    para(doc, "h es islm_money_interest_sensitivity. El superávit, medido con el gasto todavía sin recortar, empuja la tasa. Un superávit la baja. Un déficit la sube. Cada 1% del producto mueve la tasa medio punto.")
    equation(doc, "superávit  =  T − G_sin_recortar")
    equation(doc, "r  =  r_LM  −  superávit × 5.000  /  Y")
    equation(doc, "r_visible  =  recorte de r al intervalo [−50%,  +100%]")
    para(doc, "La tasa que cobran los préstamos y la que limita el crédito nuevo no es r_visible. Es su parte positiva.")
    equation(doc, "r_préstamo  =  máx(r_visible,  0)")
    equation(doc, "L_max  =  máximo del juego × 100  /  (100 + 4 × r_préstamo)")
    para(doc, "Con r_visible = −50%, r_préstamo = 0. Las empresas no pagan interés y el techo del crédito no se mueve. La política monetaria deja de llegar a la IA precisamente cuando el panel muestra la tasa más extrema.")

    heading(doc, "5.6 Precio de la deuda privada", 2)
    equation(doc, "q  =  500 × 1.000  /  máx(r_sin_recorte,  50)")
    para(doc, "El 50 del denominador es 0,50%. Si la tasa sin recortar es negativa, q se calcula como si la tasa fuera 0,50%. El precio y la tasa del panel pueden contar historias distintas. En una tasa de mercado del 5% (500 centésimas), q vale 1.000. Si la tasa baja, q sube.")

    heading(doc, "5.7 Potencial, Phillips, Okun y actividad", 2)
    para(doc, "El potencial de carga sigue a la capacidad instalada en porcentaje, a partir del primer ancla. El potencial que entra en la brecha, Y*, da cada mes un paso de un veinticuatroavo hacia un objetivo que mezcla ese potencial de carga, la población respecto de su base (entre 25% y 400%) y un factor de capital.")
    equation(doc, "factor de capital  =  recorte de (10.000 + 2K)  a  [10.000,  40.000]")
    equation(doc, "objetivo  =  potencial de carga × factor de población × factor de capital, con los factores en diezmilésimas")
    equation(doc, "Y*  ←  Y*  +  (objetivo − Y*) / 24")
    para(doc, "Si Y* y la carga entregada difieren en más de cuatro veces, Y* se sustituye por Y. La brecha queda en cero por esa sustitución, no porque la economía haya vuelto al potencial.")
    equation(doc, "brecha  =  (Y − Y*) / Y*")
    equation(doc, "π  =  recorte de (π_esperada + α × brecha_en_centésimas / 100)  a  [−50%,  +100%]")
    equation(doc, "π_esperada  ←  π_esperada + (π − π_esperada) × adaptación / 1.200")
    equation(doc, "P  ←  P × (1 + π / 12)")
    equation(doc, "desempleo  =  recorte de (5% − brecha / 2)  a  [0%,  50%]")
    para(doc, "α es islm_phillips_slope_percent. Con el valor de fábrica 50, un punto de brecha suma medio punto de inflación. La adaptación es islm_expectation_adaptation_percent y el divisor 1.200 reparte el aprendizaje a lo largo del año. La inflación de lo que se cobra en el juego es una fracción de π, el ajuste islm_payment_inflation_percent. La actividad de la carga es 100% más la brecha por el enlace islm_output_gap_cargo_link, con suelo 50% y techo 200%. Con brecha cero, π es igual a la inflación esperada, el desempleo se queda en 5% y esa actividad se queda en 100%.")
    para(doc, "Esa actividad escala la producción de pueblos e industrias. Un pueblo que crece produce entre 30% y 100% más de carga. Una industria por encima del nivel con el que nació (16) produce hasta el doble. Si hay subsidio fijado, el juego ofrece más rutas de pasajeros entre pueblos que crecen y elige industrias más productivas, y el pago extra de una entrega subvencionada entra en G.")

    heading(doc, "5.8 Deuda pública e indexación", 2)
    equation(doc, "servicio  =  suma de tramo × cupón")
    equation(doc, "si T < G + servicio,  se emite un bono al máx(r, 0)")
    equation(doc, "si T > G + servicio,  se recompra primero el cupón más caro")
    para(doc, "El cupón no puede ser negativo. Con la tasa en el suelo, la deuda que ya existe no genera intereses en el panel. En enero, el consumo autónomo, las compras, la inversión autónoma y el impuesto autónomo crecen con la inflación del año, y el ancla de demanda se corre en la misma cantidad. M y el tipo impositivo no se indexan. Como C0 e I0 no entran en el producto, indexarlos no cambia la partida, salvo por el efecto de las compras y de T0.")

    heading(doc, "5.9 Curvas que se dibujan", 2)
    para(doc, "IS y OA-DA no resuelven el mes. Se dibujan alrededor del punto ya calculado, para que la pendiente se vea.")
    equation(doc, "Y(r)  =  Y  +  (r_equilibrio − r) × máx(Y × b / 80.000,  1)")
    equation(doc, "r(Y)  =  la misma fórmula del apartado 5.5, en el Y de la muestra")
    equation(doc, "Y(P)  =  Y  +  (saldo real en P − saldo real actual) × Y / 20.000")
    para(doc, "Diez puntos de interés mueven el dibujo de la IS cerca de un cuarto del producto. La oferta de corto plazo pasa por el equilibrio con la pendiente de Phillips. La oferta de largo plazo es una vertical en Y*.")

    heading(doc, "6. La captura, número a número", 1)
    para(doc, "Estos son los valores del panel y la cuenta que los produce. Sirven de prueba de las ecuaciones y de punto de partida del plan.")
    add_table(
        doc,
        ["Lectura del panel", "Valor", "Qué ecuación lo da"],
        [
            ["Producto Y", "3.199", "Carga entregada. Coincide con el potencial porque la brecha se anuló."],
            ["Dinero real m", "1.856", "2.061 × 100.000 / 110.998."],
            ["Demanda de dinero L", "159", "50 × 3.199 / 1.000. El panel de tasas no la imprime; el cuadro de variables sí."],
            ["Tasa antes del suelo", "−89,5%", "(500 − 5.801) × 100 / 40  menos el ajuste del déficit = −8.951 centésimas."],
            ["Tasa en pantalla", "−50,00%", "Suelo del recorte. La fórmula no se detuvo ahí."],
            ["Inversión", "1.439", "Techo: 45% de 3.199."],
            ["Gasto en la identidad", "1.760", "El gasto pedido era 3.230. Se recortó a Y − I."],
            ["Consumo", "0", "3.199 − 1.439 − 1.760."],
            ["Impuestos", "478", "100 × 3.199 / 1.000 + 5% × 3.199."],
            ["Saldo G − T", "1.282", "1.760 − 478. Déficit, con el gasto ya recortado."],
            ["Inflación y desempleo", "5,10% y 5,00%", "Brecha cero: π = π esperada y Okun se queda en el 5%."],
            ["Interés de la deuda", "0", "El cupón y el préstamo usan máx(r, 0)."],
        ],
        [4.4, 3.2, 9.8],
    )
    para(doc, "El dinero real es el 58% del producto. La demanda de dinero del ajuste k = 50 es el 5% del producto. Esa distancia, no una política extrema del jugador, es lo que empuja la tasa por debajo del suelo.")

    heading(doc, "7. Inconsistencias", 1)
    para(doc, "Están ordenadas por el daño que hacen a una lectura económica de la partida. Las primeras impiden que el experimento se pueda interpretar. Las últimas dejan instrumentos y textos que no corresponden a lo que se calcula.")

    heading(doc, "7.1 La tasa vive en el suelo o en el techo", 2)
    para(doc, "El intervalo permitido es −50% a +100%. En la partida anterior la misma economía estuvo en +100% porque el producto, medido en otro numerario, hacía minúsculo al dinero. Ahora está en −50% porque el producto, medido en carga, hace enorme al dinero. En los dos casos el jugador mueve M, Y o P y la tasa no se despega del tope. Una tasa representativa se determina dentro de la banda, no por el recorte.")

    heading(doc, "7.2 El consumo es cero con empresas que ingresan", 2)
    para(doc, "La regla «sin inversión no hay consumo» no es lo que puso el consumo en cero. La inversión es positiva. El consumo desaparece porque el gasto público, al multiplicarse por Y/1.000, pide más que el producto, y el residuo se lo queda G. Una economía representativa no puede tener consumo nulo mientras la carga se entrega y las empresas cobran.")

    heading(doc, "7.3 Dos numerarios en la misma identidad", 2)
    para(doc, "Y es carga. I es una fracción de esa carga deducida de libras. G y T0 son puntos de un ajuste, estirados por Y/1.000. M está en libras nominales y m en un índice de precios aparte. C0, I0 y la propensión viven en un tercer convenio que ya no entra en la identidad. No hay una unidad en la que se pueda decir «el consumo es el 60% del producto» sin que el porcentaje sea un accidente del escalado.")

    heading(doc, "7.4 La identidad no es una conducta", 2)
    para(doc, "C + I + G = Y se impone al final. La propensión a consumir, el saldo real y la inflación atrasada, que el comentario del código todavía pone dentro del consumo, no mueven C. La filtración se calcula para guardarla y no multiplica nada. IS-LM, en el sentido de dos curvas que determinan Y y r a la vez, no se resuelve: Y llega dado por la carga y r se calcula después.")

    heading(doc, "7.5 La brecha está apagada", 2)
    para(doc, "Igualar Y* a Y cuando difieren más de cuatro veces anula Phillips, Okun y el enlace de la brecha con la producción de pueblos e industrias. En la captura los tres están en su valor de reposo: inflación igual a la esperada, desempleo del 5%, actividad de la carga al 100%. El potencial deja de ser una restricción de capacidad. La capacidad de los vehículos es 8.847 y el potencial es 3.199, y esa distancia no genera brecha.")

    heading(doc, "7.6 El superávit y el precio de la deuda no dicen lo mismo", 2)
    para(doc, "El saldo que mueve la tasa usa el gasto antes de recortarlo. El saldo que muestra el panel usa el gasto después. Pueden tener distinto signo o distinta magnitud. Además, q ignora el −50% del panel y se valora al 0,50%. Y el interés de una deuda pública de 17.616 es cero, porque un cupón negativo se sustituye por cero. Hay deuda sin servicio y un precio de esa deuda que no corresponde a la tasa impresa.")

    heading(doc, "7.7 La política monetaria no llega a las empresas", 2)
    para(doc, "El canal hacia la IA es el interés del préstamo y el techo de crédito. Los dos usan la parte positiva de la tasa. En el suelo de −50% ese canal está cerrado: endeudarse es gratis y el techo es el de siempre. Subir o bajar M no cambia lo que una compañía puede construir. El experimento monetario no tiene transmisión.")

    heading(doc, "7.8 Instrumentos que se ven y no actúan", 2)
    para(doc, "C0 e I0 se muestran, se indexan cada enero y no entran en C ni en I. La propensión a consumir solo aparece en la indexación del impuesto autónomo. El impulso exógeno del panel está fijado en cero: la línea «producto = juego + impulso» es Y = Y + 0. El comentario del fuente describe un IS-LM que este solucionador ya no calcula. Quien lea el panel y quien lea el código no están viendo el mismo modelo.")

    heading(doc, "8. Plan director", 1)
    para(doc, "El objetivo es una economía cerrada que se pueda leer en la partida. La tasa de mercado vive en una banda donde el dinero, el producto y el saldo público la mueven. El préstamo que pagan las empresas no es negativo. El consumo, la inversión y el gasto son los tres positivos cuando la red entrega carga, y suman el producto. La brecha puede ser distinta de cero, de modo que precios, desempleo y actividad respondan. El trabajo se hace por paquetes. Cada uno deja una ecuación de llegada y una prueba con la partida de la captura. No se abre el paquete siguiente si el anterior no cumple la prueba.")

    heading(doc, "Paquete 1. Un solo numerario", 2)
    para(doc, "Elegir la unidad de carga entregada como unidad de todas las magnitudes de la identidad. Las libras solo sirven para medir, dentro del juego, cuánta carga representa un gasto.")
    equation(doc, "precio de una unidad  =  R  /  Y     si Y > 0 y R > 0")
    equation(doc, "I  =  I_libras  /  precio de una unidad")
    equation(doc, "G  =  (compras en libras equivalentes + subsidios pagados)  /  precio de una unidad")
    para(doc, "Se abandona el factor Y/1.000 sobre los puntos de los ajustes. Si un instrumento sigue siendo un porcentaje, se declara como porcentaje de Y y se aplica una sola vez. C0, I0, T0 y M se expresan en la misma unidad que Y, o se retiran del panel si no entran en ninguna ecuación.")
    para(doc, "Prueba. Con la red actual, I es una fracción reconocible del ingreso (del orden del gasto de mantenimiento y de obra sobre los ingresos), no el techo automático del 45%. G no supera por sí solo al producto.")

    heading(doc, "Paquete 2. Una tasa interior", 2)
    para(doc, "La demanda de dinero en el equilibrio de tasa cero es una proporción k del producto, con k leído como porcentaje, no como el 5% escondido en el factor 10. La tasa, en centésimas de punto, es la distancia entre esa demanda y el dinero real, escalada por h y por el propio producto para que no dependa de si Y vale miles o cientos de miles.")
    equation(doc, "r_bp  =  (k × Y / 100  −  m)  ×  10.000  /  (h × Y)")
    para(doc, "Con los números de la captura y la aritmética entera del juego, kY/100 vale 1.599. (1.599 − 1.856) × 10.000 / (40 × 3.199) = −20 centésimas, es decir −0,20%. Es una tasa baja, del lado del dinero abundante, y no un suelo. Subir M la baja un poco más. Bajar M o subir Y la lleva a terreno positivo. El recorte de seguridad puede quedar en 0% y 25% para la tasa de mercado, lejos de −50% y de +100%, y solo debe actuar si los ajustes se llevan a un extremo que el jugador no usa.")
    para(doc, "El superávit entra como un desplazamiento pequeño y en la misma unidad: un superávit de un 1% de Y baja la tasa cerca de medio punto, y un déficit la sube otro tanto. El saldo que entra aquí es el mismo saldo que se imprime.")
    equation(doc, "r_bp  =  r_bp del dinero  −  50 × (T − G) / Y × 100")
    para(doc, "Prueba. Al abrir la partida la tasa no está en −50% ni en +100%. Mover el dinero de 2.061 hacia arriba y hacia abajo mueve la tasa en décimas o en puntos, dentro de la banda. El panel y la curva LM usan la misma fórmula.")

    heading(doc, "Paquete 3. Consumo con conducta, no como residuo exprimido", 2)
    para(doc, "El ingreso que la inversión produce es la carga entregada, valorada por lo que las empresas cobraron. Los hogares consumen una fracción de ese ingreso después de impuestos. La inversión es el gasto medido en el paquete 1, sin un techo que deje a G el resto del producto. G es el gasto del paquete 1. Si la suma pasa de Y, se registra la diferencia como existencias o como un desequilibrio, y no se borra el consumo.")
    equation(doc, "C  =  c × (Y − T)     si I > 0")
    equation(doc, "C  =  0               si I = 0")
    equation(doc, "Y  =  C + I + G + desequilibrio")
    para(doc, "La propensión c vuelve a ser el ajuste que su nombre anuncia. El saldo real puede sumar un término acotado a C, como en Patinkin, del orden de una fracción de (m − m de referencia), no un salto capaz de dominar el producto. La inflación ya realizada puede adelantar una parte pequeña de ese consumo. Los dos términos estaban en el diseño original y hoy no ejecutan.")
    para(doc, "Prueba. Con entrega de carga e inversión positiva, C, I y G son los tres mayores que cero y el desequilibrio es pequeño frente a Y. Bajar c baja C. Subir el gasto público no pone el consumo en cero.")

    heading(doc, "Paquete 4. Devolver la brecha", 2)
    para(doc, "Y* deja de copiar a Y. El potencial es la capacidad de entrega de la red: la carga que los vehículos pueden mover, corregida por población y por el capital acumulado, con el ajuste lento de un veinticuatroavo que ya existe. La sustitución «si difieren más de cuatro veces, Y* = Y» se elimina. La brecha puede ser negativa si se entrega poca carga respecto de la flota, y positiva si la flota va justa.")
    equation(doc, "brecha  =  (Y − Y*) / Y*")
    equation(doc, "π  =  π_esperada  +  α × brecha")
    equation(doc, "desempleo  =  5%  −  brecha / 2")
    para(doc, "Phillips y Okun se quedan como están en la forma. Lo que cambia es que la brecha vuelve a ser una distancia real entre carga entregada y capacidad. La actividad de pueblos e industrias sigue leyendo esa brecha, de modo que un auge produce más carga y una recesión menos.")
    para(doc, "Prueba. En la captura, Y* queda cerca de la capacidad (8.847) o de una fracción estable de ella, no clavado en 3.199. La brecha no es 0,00% por construcción. La inflación se separa de la esperada y el desempleo se separa del 5% en cuanto pasa un mes.")

    heading(doc, "Paquete 5. Deuda, cupón y precio coherentes con la tasa", 2)
    para(doc, "Un solo saldo público entra en la tasa, en el panel y en la emisión de bonos. El cupón de un bono nuevo es la tasa de mercado cuando esa tasa es positiva, y cero cuando es negativa: el Tesoro no paga un cupón negativo ni cobra al tenedor por el hecho de haber prestado, pero la deuda ya emitida sigue devengando el cupón que se pactó. q usa la misma tasa que se imprime, con un suelo de cálculo solo para no dividir por cero, y ese suelo se declara en el panel si llega a usarse.")
    equation(doc, "q  =  1.000 × 5%  /  máx(r_impresa,  0,50%)")
    para(doc, "Así, una baja de la tasa sube el precio de la deuda privada, que es lo que se pidió del superávit, y el precio no se calcula con una tasa distinta de la que se lee.")
    para(doc, "Prueba. Con la tasa del paquete 2, el interés de la deuda pública no es cero mientras existan bonos con cupón. Un superávit baja r y sube q. El saldo del panel coincide con el saldo de la ecuación de la tasa.")

    heading(doc, "Paquete 6. Transmisión a las compañías y a la IA", 2)
    para(doc, "La tasa del préstamo es máx(r_impresa, 0). No se paga a una empresa por endeudarse. El techo de crédito sigue siendo el que ya está: a más tasa positiva, menos capacidad de pedir prestado, sin reclamar el préstamo ya tomado. Como la tasa del paquete 2 ya no está en el suelo, ese techo y el interés mensual vuelven a cambiar cuando se mueve M. Los incentivos locales se mantienen: más carga donde el pueblo crece, más carga donde la industria produce por encima de su nivel de origen, y más competencia donde hay subsidio.")
    para(doc, "Prueba. Con solo compañías de la IA, un alza de M baja el interés que pagan y amplía el crédito; una baja de M hace lo contrario. La inversión en libras del mes siguiente se mueve en ese sentido. No hace falta una empresa humana.")

    heading(doc, "Paquete 7. Que el panel, el comentario y los ajustes digan lo mismo", 2)
    para(doc, "El comentario al inicio de macro_economy.cpp se reescribe con las ecuaciones de los paquetes 1 a 6. C0 e I0, si siguen en la ventana, entran en C y en I como dicen sus nombres, o salen de la ventana. El impulso exógeno deja de estar fijado en cero: es el cambio de Y que producen M, G, t y T0 respecto del ancla, medido en carga. Cada serie sigue teniendo su gráfica, y la IS y la LM se dibujan con las mismas funciones que resuelven el mes, no con una pendiente de adorno.")
    para(doc, "Prueba. Leer el panel y leer el fuente da la misma lista de ecuaciones. Mover un ajuste exógeno mueve el impulso y, donde corresponde, la tasa o el producto. Ninguna línea del panel suma cifras que el desglose no pueda reconstruir.")

    heading(doc, "9. Criterio de cierre", 1)
    para(doc, "El plan está cerrado cuando, sobre la misma partida de la captura, se cumplen a la vez estas lecturas.")
    bullet(doc, "La tasa no está en −50% ni en +100%. Con M = 2.061, Y cerca de 3.199 y m cerca de 1.856, queda en torno a cero o ligeramente por debajo, y responde al deslizador del dinero.", numbered=True)
    bullet(doc, "Consumo, inversión y gasto son los tres positivos y su suma, más un desequilibrio pequeño, es la carga entregada.", numbered=True)
    bullet(doc, "El potencial no está copiado del producto del mes. La brecha, la inflación y el desempleo pueden separarse de 0%, de la inflación esperada y del 5%.", numbered=True)
    bullet(doc, "Un superávit baja la tasa y sube el precio de la deuda privada. El saldo que se ve es el saldo que entra en la tasa. La deuda viva paga el cupón que se pactó.", numbered=True)
    bullet(doc, "Las compañías de la IA pagan máx(r, 0) y su techo de crédito se mueve con esa tasa. El experimento puede correr sin una empresa humana.", numbered=True)

    heading(doc, "10. Qué permanece", 1)
    para(doc, "Sigue siendo una economía cerrada, mensual y entera. Sigue leyendo a todas las compañías. La carga entregada sigue siendo el producto. La inversión sigue incluyendo la obra y el mantenimiento de la infraestructura, porque sin esa red no hay ingreso. El largo plazo sigue siendo una Phillips con expectativas adaptativas y un Okun sobre la misma brecha. Los pueblos que crecen, las industrias que producen más y las rutas subvencionadas siguen atrayendo actividad. Lo que el plan cambia es el numerario, la tasa y el hecho de que el consumo y la brecha vuelvan a ser resultado de la partida y no del recorte.")

    para(doc, "Fin del diagnóstico. El modelo no se ha modificado al escribir este documento.", size=10, italic=True, space_before=12)

    doc.save(OUT)
    import zipfile
    from io import BytesIO
    buf = BytesIO()
    with zipfile.ZipFile(OUT, "r") as zin, zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == "word/settings.xml":
                text = data.decode("utf-8")
                text = text.replace('<w:zoom w:val="bestFit"/>', '<w:zoom w:percent="100"/>')
                data = text.encode("utf-8")
            zout.writestr(item, data)
    with open(OUT, "wb") as handle:
        handle.write(buf.getvalue())
    print(OUT)


if __name__ == "__main__":
    build()
