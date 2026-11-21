.pragma library

// LegionGames visual tokens and formatters.
// Mirrors plans/03-moonlight-ux.md section 2.4. This is a plain JS library
// (.pragma library) so it must not touch context properties; use Api.js for
// anything that reads `legionApi`.

// Surfaces
var bg = "#0d0d0d"
var panel = "#1a1a1a"
var panelAlt = "#111111"
var panelHover = "#222222"
var line = "#333333"
var lineStrong = "#3a3a3a"

// Text
var text = "#e6e6e6"
var textMuted = "#85858f"
var textDim = "#6b6b74"
var textOnAccent = "#ffffff"

// Accent and semantics
var accent = "#c92a2a"
var accentHover = "#a01f1f"
var accentPressed = "#8f1c1c"
var success = "#7fd18b"
var warning = "#f0a500"
var error = "#ff6b6b"
var focus = "#ff6b6b"

// Spacing
var spaceXs = 4
var spaceS = 8
var spaceM = 12
var spaceL = 16
var spaceXl = 24
var spaceXxl = 32

// Radii
var radiusS = 8
var radiusM = 12
var radiusL = 15

// Font sizes (pointSize)
var fontCaption = 9
var fontSmall = 10
var fontBody = 12
var fontSubtitle = 14
var fontTitle = 18
var fontHeader = 24
var fontHero = 30

// Catalog data older than this many seconds disables start and payment.
var staleSeconds = 600

function pad2(value) {
    var n = Math.floor(Number(value))
    if (!isFinite(n) || n < 0) {
        n = 0
    }
    return n < 10 ? "0" + n : String(n)
}

function number(value) {
    var n = Number(value)
    return isFinite(n) ? n : 0
}

// 500.00 -> "500 ₽"; 150.50 -> "150,50 ₽"
function money(value) {
    var n = number(value)
    var rounded = Math.round(n * 100) / 100
    var body = (rounded % 1 === 0) ? String(rounded)
                                   : rounded.toFixed(2).replace(".", ",")
    return body + " ₽"
}

function pricePerHour(value) {
    return money(value) + "/ч"
}

// 8060 -> "2 ч 14 мин"; 120 -> "2 мин"; 45 -> "45 с"
function duration(seconds) {
    var total = Math.max(0, Math.floor(number(seconds)))
    if (total < 60) {
        return total + " с"
    }
    var minutes = Math.floor(total / 60)
    if (minutes < 60) {
        return minutes + " мин"
    }
    var hours = Math.floor(minutes / 60)
    var rest = minutes % 60
    return rest === 0 ? (hours + " ч") : (hours + " ч " + rest + " мин")
}

// 8060 -> "02:14:36"
function clock(seconds) {
    var total = Math.max(0, Math.floor(number(seconds)))
    var hours = Math.floor(total / 3600)
    var minutes = Math.floor((total % 3600) / 60)
    return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(total % 60)
}

// 8060 -> "134:36" (claim countdowns are short, but stay safe)
function clockShort(seconds) {
    var total = Math.max(0, Math.floor(number(seconds)))
    return pad2(Math.floor(total / 60)) + ":" + pad2(total % 60)
}

// Python isoformat may carry microseconds; the QML engine parses milliseconds.
function parseDate(value) {
    if (!value) {
        return null
    }
    var raw = String(value)
    if (raw.length === 0) {
        return null
    }
    raw = raw.replace(/(\.\d{3})\d+/, "$1")
    var date = new Date(raw)
    if (isNaN(date.getTime())) {
        return null
    }
    return date
}

function ageSeconds(value) {
    var date = parseDate(value)
    if (date === null) {
        return -1
    }
    return Math.floor((new Date().getTime() - date.getTime()) / 1000)
}

function isStale(value) {
    var age = ageSeconds(value)
    if (age < 0) {
        return true
    }
    return age > staleSeconds
}

function ageText(value) {
    var age = ageSeconds(value)
    if (age < 0) {
        return "нет данных"
    }
    if (age < 60) {
        return "только что"
    }
    if (age < 3600) {
        return (Math.floor(age / 60) + " мин назад")
    }
    if (age < 86400) {
        return (Math.floor(age / 3600) + " ч назад")
    }
    return (Math.floor(age / 86400) + " дн назад")
}

function timeText(value) {
    var date = parseDate(value)
    if (date === null) {
        return "—"
    }
    return pad2(date.getHours()) + ":" + pad2(date.getMinutes())
}

function dateTimeText(value) {
    var date = parseDate(value)
    if (date === null) {
        return "—"
    }
    return pad2(date.getDate()) + "." + pad2(date.getMonth() + 1) + ", " +
           pad2(date.getHours()) + ":" + pad2(date.getMinutes())
}

function plural(count, one, few, many) {
    var n = Math.abs(Math.floor(number(count))) % 100
    var n1 = n % 10
    if (n > 10 && n < 20) {
        return many
    }
    if (n1 > 1 && n1 < 5) {
        return few
    }
    if (n1 === 1) {
        return one
    }
    return many
}

// Status tone for StatusPill: neutral | good | warn | bad | accent
function toneForCount(free) {
    if (number(free) <= 0) {
        return "bad"
    }
    if (number(free) === 1) {
        return "warn"
    }
    return "good"
}
