/* ANNA's labels in the game's language. The game keeps its current table code (EN, DE, CHT...) as a
   std::string at +0x30 of its string tables ([0x4e7dda8], vtable 0x467d858); English when it has
   none yet or uses EN/GB. Columns: DE FR IT ES MX BR NL PL CZ HU RU JP KO CHS CHT. */
#include "anna.h"

static const char *const codes[] = {"DE", "FR", "IT", "ES", "MX", "BR", "NL", "PL",
                                    "CZ", "HU", "RU", "JP", "KO", "CHS", "CHT"};
enum { LANGUAGES = 15 };

typedef struct {
    const char *english;
    const char *text[LANGUAGES];
} Row;

static const Row rows[] = {
    {"Car", {"Auto", "Voiture", "Auto", "Coche", "Auto", "Carro", "Auto", "Samochód", "Auto", "Autó",
             "Машина", "車", "차량", "车辆", "車輛"}},
    {"Lights", {"Lichter", "Feux", "Luci", "Luces", "Luces", "Luzes", "Lichten", "Światła", "Světla",
                "Lámpák", "Фары", "ライト", "조명", "灯光", "燈光"}},
    {"Doors", {"Türen", "Portières", "Portiere", "Puertas", "Puertas", "Portas", "Deuren", "Drzwi",
               "Dveře", "Ajtók", "Двери", "ドア", "도어", "车门", "車門"}},
    {"Open Roof", {"Dach öffnen", "Ouvrir le toit", "Apri il tetto", "Abrir techo", "Abrir techo",
                   "Abrir teto", "Dak openen", "Otwórz dach", "Otevřít střechu", "Tető nyitása",
                   "Открыть крышу", "ルーフを開ける", "루프 열기", "打开车顶", "打開車頂"}},
    {"Close Roof", {"Dach schließen", "Fermer le toit", "Chiudi il tetto", "Cerrar techo",
                    "Cerrar techo", "Fechar teto", "Dak sluiten", "Zamknij dach", "Zavřít střechu",
                    "Tető zárása", "Закрыть крышу", "ルーフを閉める", "루프 닫기", "关闭车顶",
                    "關閉車頂"}},
    {"Open Hood", {"Motorhaube öffnen", "Ouvrir le capot", "Apri il cofano", "Abrir capó",
                   "Abrir cofre", "Abrir capô", "Motorkap openen", "Otwórz maskę", "Otevřít kapotu",
                   "Motorháztető nyitása", "Открыть капот", "ボンネットを開ける", "보닛 열기",
                   "打开引擎盖", "打開引擎蓋"}},
    {"Close Hood", {"Motorhaube schließen", "Fermer le capot", "Chiudi il cofano", "Cerrar capó",
                    "Cerrar cofre", "Fechar capô", "Motorkap sluiten", "Zamknij maskę",
                    "Zavřít kapotu", "Motorháztető zárása", "Закрыть капот", "ボンネットを閉める",
                    "보닛 닫기", "关闭引擎盖", "關閉引擎蓋"}},
    {"Open Trunk", {"Kofferraum öffnen", "Ouvrir le coffre", "Apri il bagagliaio",
                    "Abrir maletero", "Abrir cajuela", "Abrir porta-malas", "Kofferbak openen",
                    "Otwórz bagażnik", "Otevřít kufr", "Csomagtartó nyitása", "Открыть багажник",
                    "トランクを開ける", "트렁크 열기", "打开后备箱", "打開後車廂"}},
    {"Close Trunk", {"Kofferraum schließen", "Fermer le coffre", "Chiudi il bagagliaio",
                     "Cerrar maletero", "Cerrar cajuela", "Fechar porta-malas", "Kofferbak sluiten",
                     "Zamknij bagażnik", "Zavřít kufr", "Csomagtartó zárása", "Закрыть багажник",
                     "トランクを閉める", "트렁크 닫기", "关闭后备箱", "關閉後車廂"}},
    {"Open Front Left Door",
     {"Vordere linke Tür öffnen", "Ouvrir la portière avant gauche",
      "Apri la portiera anteriore sinistra", "Abrir puerta delantera izquierda",
      "Abrir puerta delantera izquierda", "Abrir porta dianteira esquerda", "Linkervoordeur openen",
      "Otwórz lewe przednie drzwi", "Otevřít levé přední dveře", "Bal első ajtó nyitása",
      "Открыть левую переднюю дверь", "左フロントドアを開ける", "왼쪽 앞문 열기", "打开左前门",
      "打開左前門"}},
    {"Close Front Left Door",
     {"Vordere linke Tür schließen", "Fermer la portière avant gauche",
      "Chiudi la portiera anteriore sinistra", "Cerrar puerta delantera izquierda",
      "Cerrar puerta delantera izquierda", "Fechar porta dianteira esquerda",
      "Linkervoordeur sluiten", "Zamknij lewe przednie drzwi", "Zavřít levé přední dveře",
      "Bal első ajtó zárása", "Закрыть левую переднюю дверь", "左フロントドアを閉める",
      "왼쪽 앞문 닫기", "关闭左前门", "關閉左前門"}},
    {"Open Front Right Door",
     {"Vordere rechte Tür öffnen", "Ouvrir la portière avant droite",
      "Apri la portiera anteriore destra", "Abrir puerta delantera derecha",
      "Abrir puerta delantera derecha", "Abrir porta dianteira direita", "Rechtervoordeur openen",
      "Otwórz prawe przednie drzwi", "Otevřít pravé přední dveře", "Jobb első ajtó nyitása",
      "Открыть правую переднюю дверь", "右フロントドアを開ける", "오른쪽 앞문 열기", "打开右前门",
      "打開右前門"}},
    {"Close Front Right Door",
     {"Vordere rechte Tür schließen", "Fermer la portière avant droite",
      "Chiudi la portiera anteriore destra", "Cerrar puerta delantera derecha",
      "Cerrar puerta delantera derecha", "Fechar porta dianteira direita",
      "Rechtervoordeur sluiten", "Zamknij prawe przednie drzwi", "Zavřít pravé přední dveře",
      "Jobb első ajtó zárása", "Закрыть правую переднюю дверь", "右フロントドアを閉める",
      "오른쪽 앞문 닫기", "关闭右前门", "關閉右前門"}},
    {"Open Rear Left Door",
     {"Hintere linke Tür öffnen", "Ouvrir la portière arrière gauche",
      "Apri la portiera posteriore sinistra", "Abrir puerta trasera izquierda",
      "Abrir puerta trasera izquierda", "Abrir porta traseira esquerda", "Linkerachterdeur openen",
      "Otwórz lewe tylne drzwi", "Otevřít levé zadní dveře", "Bal hátsó ajtó nyitása",
      "Открыть левую заднюю дверь", "左リアドアを開ける", "왼쪽 뒷문 열기", "打开左后门",
      "打開左後門"}},
    {"Close Rear Left Door",
     {"Hintere linke Tür schließen", "Fermer la portière arrière gauche",
      "Chiudi la portiera posteriore sinistra", "Cerrar puerta trasera izquierda",
      "Cerrar puerta trasera izquierda", "Fechar porta traseira esquerda",
      "Linkerachterdeur sluiten", "Zamknij lewe tylne drzwi", "Zavřít levé zadní dveře",
      "Bal hátsó ajtó zárása", "Закрыть левую заднюю дверь", "左リアドアを閉める",
      "왼쪽 뒷문 닫기", "关闭左后门", "關閉左後門"}},
    {"Open Rear Right Door",
     {"Hintere rechte Tür öffnen", "Ouvrir la portière arrière droite",
      "Apri la portiera posteriore destra", "Abrir puerta trasera derecha",
      "Abrir puerta trasera derecha", "Abrir porta traseira direita", "Rechterachterdeur openen",
      "Otwórz prawe tylne drzwi", "Otevřít pravé zadní dveře", "Jobb hátsó ajtó nyitása",
      "Открыть правую заднюю дверь", "右リアドアを開ける", "오른쪽 뒷문 열기", "打开右后门",
      "打開右後門"}},
    {"Close Rear Right Door",
     {"Hintere rechte Tür schließen", "Fermer la portière arrière droite",
      "Chiudi la portiera posteriore destra", "Cerrar puerta trasera derecha",
      "Cerrar puerta trasera derecha", "Fechar porta traseira direita",
      "Rechterachterdeur sluiten", "Zamknij prawe tylne drzwi", "Zavřít pravé zadní dveře",
      "Jobb hátsó ajtó zárása", "Закрыть правую заднюю дверь", "右リアドアを閉める",
      "오른쪽 뒷문 닫기", "关闭右后门", "關閉右後門"}},
    {"Open All", {"Alle öffnen", "Tout ouvrir", "Apri tutto", "Abrir todo", "Abrir todo",
                  "Abrir tudo", "Alles openen", "Otwórz wszystko", "Otevřít vše", "Összes nyitása",
                  "Открыть всё", "すべて開ける", "모두 열기", "全部打开", "全部打開"}},
    {"Close All", {"Alle schließen", "Tout fermer", "Chiudi tutto", "Cerrar todo", "Cerrar todo",
                   "Fechar tudo", "Alles sluiten", "Zamknij wszystko", "Zavřít vše",
                   "Összes zárása", "Закрыть всё", "すべて閉める", "모두 닫기", "全部关闭",
                   "全部關閉"}},
    {"Headlights On", {"Scheinwerfer an", "Allumer les phares", "Accendi i fari", "Encender faros",
                       "Encender faros", "Ligar faróis", "Koplampen aan", "Włącz reflektory",
                       "Zapnout světlomety", "Fényszórók be", "Включить фары",
                       "ヘッドライトをオン", "헤드라이트 켜기", "打开前大灯", "打開頭燈"}},
    {"Headlights Off", {"Scheinwerfer aus", "Éteindre les phares", "Spegni i fari",
                        "Apagar faros", "Apagar faros", "Desligar faróis", "Koplampen uit",
                        "Wyłącz reflektory", "Vypnout světlomety", "Fényszórók ki",
                        "Выключить фары", "ヘッドライトをオフ", "헤드라이트 끄기", "关闭前大灯",
                        "關閉頭燈"}},
    {"Hazard Lights On",
     {"Warnblinker an", "Allumer les feux de détresse", "Accendi le quattro frecce",
      "Encender luces de emergencia", "Encender luces intermitentes", "Ligar pisca-alerta",
      "Alarmlichten aan", "Włącz światła awaryjne", "Zapnout výstražná světla", "Vészvillogó be",
      "Включить аварийные огни", "ハザードランプをオン", "비상등 켜기", "打开双闪", "打開警示燈"}},
    {"Hazard Lights Off",
     {"Warnblinker aus", "Éteindre les feux de détresse", "Spegni le quattro frecce",
      "Apagar luces de emergencia", "Apagar luces intermitentes", "Desligar pisca-alerta",
      "Alarmlichten uit", "Wyłącz światła awaryjne", "Vypnout výstražná světla", "Vészvillogó ki",
      "Выключить аварийные огни", "ハザードランプをオフ", "비상등 끄기", "关闭双闪",
      "關閉警示燈"}},
    {"Left Indicator On",
     {"Blinker links an", "Allumer le clignotant gauche", "Accendi la freccia sinistra",
      "Encender intermitente izquierdo", "Encender direccional izquierda", "Ligar seta esquerda",
      "Richtingaanwijzer links aan", "Włącz lewy kierunkowskaz", "Zapnout levý blinkr",
      "Bal index be", "Включить левый поворотник", "左ウインカーをオン", "왼쪽 방향지시등 켜기",
      "打开左转向灯", "打開左方向燈"}},
    {"Left Indicator Off",
     {"Blinker links aus", "Éteindre le clignotant gauche", "Spegni la freccia sinistra",
      "Apagar intermitente izquierdo", "Apagar direccional izquierda", "Desligar seta esquerda",
      "Richtingaanwijzer links uit", "Wyłącz lewy kierunkowskaz", "Vypnout levý blinkr",
      "Bal index ki", "Выключить левый поворотник", "左ウインカーをオフ", "왼쪽 방향지시등 끄기",
      "关闭左转向灯", "關閉左方向燈"}},
    {"Right Indicator On",
     {"Blinker rechts an", "Allumer le clignotant droit", "Accendi la freccia destra",
      "Encender intermitente derecho", "Encender direccional derecha", "Ligar seta direita",
      "Richtingaanwijzer rechts aan", "Włącz prawy kierunkowskaz", "Zapnout pravý blinkr",
      "Jobb index be", "Включить правый поворотник", "右ウインカーをオン",
      "오른쪽 방향지시등 켜기", "打开右转向灯", "打開右方向燈"}},
    {"Right Indicator Off",
     {"Blinker rechts aus", "Éteindre le clignotant droit", "Spegni la freccia destra",
      "Apagar intermitente derecho", "Apagar direccional derecha", "Desligar seta direita",
      "Richtingaanwijzer rechts uit", "Wyłącz prawy kierunkowskaz", "Vypnout pravý blinkr",
      "Jobb index ki", "Выключить правый поворотник", "右ウインカーをオフ",
      "오른쪽 방향지시등 끄기", "关闭右转向灯", "關閉右方向燈"}},
    {"Enable Auto Drive", {"Autopilot aktivieren", "Activer la conduite auto", "Attiva guida automatica",
                           "Activar conducción automática", "Activar manejo automático", "Ativar direção automática",
                           "Automatisch rijden aan", "Włącz autopilota", "Zapnout autopilota",
                           "Robotpilóta bekapcsolása", "Включить автопилот", "自動運転を有効にする", "자동 주행 켜기",
                           "启用自动驾驶", "啟用自動駕駛"}},
    {"Disable Auto Drive", {"Autopilot deaktivieren", "Désactiver la conduite auto", "Disattiva guida automatica",
                            "Desactivar conducción automática", "Desactivar manejo automático",
                            "Desativar direção automática", "Automatisch rijden uit", "Wyłącz autopilota",
                            "Vypnout autopilota", "Robotpilóta kikapcsolása", "Выключить автопилот",
                            "自動運転を無効にする", "자동 주행 끄기", "停用自动驾驶", "停用自動駕駛"}},
    /* The tag above ANNA while she drives: "Auto Drive", then "On" in her colour. */
    {"Auto Drive", {"Autopilot", "Conduite auto", "Guida automatica", "Conducción automática",
                    "Manejo automático", "Direção automática", "Automatisch rijden", "Autopilot",
                    "Autopilot", "Robotpilóta", "Автопилот", "自動運転", "자동 주행", "自动驾驶",
                    "自動駕駛"}},
    {"On", {"An", "Activée", "Attiva", "Activada", "Activado", "Ligada", "Aan", "Włączony", "Zapnut",
            "Be", "Вкл.", "オン", "켜짐", "开启", "開啟"}},
    /* Auto Drive's prompts: {} is where the button's picture goes. */
    {"Press {} to activate Cinematic Camera",
     {"Drücke {}, um die Kinokamera zu aktivieren", "Appuyez sur {} pour activer la caméra cinématique", "Premi {} per attivare la telecamera cinematografica", "Pulsa {} para activar la cámara cinemática", "Presiona {} para activar la cámara cinemática", "Pressione {} para ativar a câmera cinematográfica", "Druk op {} om de filmische camera aan te zetten", "Naciśnij {}, aby włączyć kamerę filmową", "Stiskněte {} pro zapnutí filmové kamery", "Nyomd meg: {} – filmes kamera", "Нажмите {}, чтобы включить кинокамеру", "{}でシネマカメラを起動", "{}을(를) 눌러 시네마틱 카메라 켜기", "按 {} 启用电影镜头", "按 {} 啟用電影鏡頭"}},
    {"Ask ANNA {} to disable Auto Drive",
     {"Bitte ANNA {}, den Autopiloten zu deaktivieren", "Demandez à ANNA {} de désactiver la conduite auto", "Chiedi ad ANNA {} di disattivare la guida automatica", "Pide a ANNA {} que desactive la conducción automática", "Pídele a ANNA {} que desactive el manejo automático", "Peça à ANNA {} para desativar a direção automática", "Vraag ANNA {} om automatisch rijden uit te zetten", "Poproś ANNA {} o wyłączenie autopilota", "Požádejte ANNA {} o vypnutí autopilota", "ANNA {} – robotpilóta kikapcsolása", "Попросите ANNA {} выключить автопилот", "ANNA {}で自動運転を無効にする", "ANNA {}로 자동 주행 끄기", "请 ANNA {} 停用自动驾驶", "請 ANNA {} 停用自動駕駛"}}
};

static int language(void) {
    unsigned char *tables;
    void *vtable;
    MsvcString code;
    if (!memory_read_pointer(image + 0x4e7dda8, (void **)&tables) ||
        !memory_read_pointer(tables, &vtable) || vtable != image + 0x467d858 ||
        !memory_read(tables + 0x30, &code, sizeof(code)) || code.capacity != 15 || code.size < 2 ||
        code.size > 3 || code.text[code.size])
        return -1;
    for (int i = 0; i < LANGUAGES; ++i)
        if (text_length(codes[i]) == code.size && memory_equal(codes[i], code.text, code.size)) return i;
    return -1;
}

const char *text_get(const char *english) {
    int at = language();
    if (at < 0) return english;
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (!lstrcmpA(rows[i].english, english)) return rows[i].text[at];
    return english;
}
