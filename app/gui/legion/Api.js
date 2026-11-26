.pragma library

// Facade over the `legionApi` QML context property supplied by the account
// backend (app/account/legionapi.h). Nothing here may assume a name that the
// backend does not publish: every call is guarded so a missing method degrades
// to a readable error instead of a QML exception.
//
// Properties read by the UI:
//   state, handle, onboardingStep, busy, lastError, lastErrorCode, words,
//   catalogUpdatedAt, device, legalDocuments, ageNotice, installPath
// Invokables used here:
//   setConsent, submitConsent, registerAccount (legacy alias: register), login,
//   logout, confirmWordsSaved, reissueWords, reprintRecoveryKit, recoverComplete,
//   refreshMe, getClubs, getClub, getWallet, createTopup, pollIntent,
//   reissueHandoff, startSession, getCurrentSession, stopSession,
//   extendSession, reconnectSession, getQueue, joinQueue, leaveQueue, claimQueue,
//   getLegalDocuments, deleteAccount, openExternal, openLogFolder,
//   retryFailedStep
// Signals consumed by views:
//   stateChanged, wordsReady(handle, words), recoveryKitWritten(path,
//   syncedFolder), consentRequired, paymentReturned(publicId),
//   response(op, data), failed(op, code, message)
//
// Response ops handled by the UI:
//   clubs, club, wallet, topup, intent, handoff, session_start,
//   session_current, session_stop, session_extend, session_reconnect, queue,
//   queue_join, queue_leave, queue_claim, confirm_words, refresh_me,
//   account_delete, legal_documents, kit, open_logs
// getWalletHistory / `wallet_history` exists on the backend but has no UI
// section yet: the ledger item schema is not part of Contract v1.4.

var staleSeconds = 600

function available(api) {
    return api !== null && api !== undefined
}

function has(api, name) {
    if (!available(api)) {
        return false
    }
    return typeof api[name] === "function"
}

function call0(api, name) {
    if (!has(api, name)) {
        return false
    }
    api[name]()
    return true
}

function call1(api, name, a) {
    if (!has(api, name)) {
        return false
    }
    api[name](a)
    return true
}

function call2(api, name, a, b) {
    if (!has(api, name)) {
        return false
    }
    api[name](a, b)
    return true
}

function call3(api, name, a, b, c) {
    if (!has(api, name)) {
        return false
    }
    api[name](a, b, c)
    return true
}

// ---- auth and onboarding ------------------------------------------------

function setConsent(api, agreement, pdnConsent) {
    return call2(api, "setConsent", agreement === true, pdnConsent === true)
}

// The backend invokable is registerAccount(); `register` is a C++ keyword and
// was only a draft name. Direct property access on purpose: registration is
// the one call that must not depend on the generic dynamic dispatcher.
function register(api, label) {
    if (!available(api)) {
        return false
    }
    if (typeof api.registerAccount === "function") {
        api.registerAccount(label)
        return true
    }
    if (typeof api.register === "function") {
        api.register(label)
        return true
    }
    return false
}

// Submits the consent recorded by setConsent for an existing account.
function submitConsent(api) {
    return call0(api, "submitConsent")
}

function login(api) {
    return call0(api, "login")
}

function logout(api) {
    return call0(api, "logout")
}

function confirmWordsSaved(api) {
    return call0(api, "confirmWordsSaved")
}

function reissueWords(api) {
    return call0(api, "reissueWords")
}

// Rewrites the recovery kit file without rotating the words.
function reprintRecoveryKit(api) {
    return call0(api, "reprintRecoveryKit")
}

// Drops the local vault, device key and tokens. Used when the local key blob is
// unreadable and the user chooses to start over.
function resetLocalAccount(api) {
    return call0(api, "resetLocalAccount")
}

// Checks the local NetBird CLI and reports whether it is connected to the
// management server. Used by the Settings network block.
function checkNetbirdStatus(api) {
    return call0(api, "checkNetbirdStatus")
}

function hasReprintRecoveryKit(api) {
    return has(api, "reprintRecoveryKit")
}

// Web-only verification path is not used by the app UI; the app completes
// recovery with recoverComplete below.
function recoverComplete(api, handle, words, label) {
    return call3(api, "recoverComplete", handle, words, label)
}

function refreshMe(api) {
    return call0(api, "refreshMe")
}

// ---- legal documents ----------------------------------------------------

function getLegalDocuments(api) {
    return call0(api, "getLegalDocuments")
}

// ---- catalog ------------------------------------------------------------

function getClubs(api, availableOnly) {
    return call1(api, "getClubs", availableOnly === true)
}

function getClub(api, slug) {
    return call1(api, "getClub", slug)
}

// ---- wallet and payments ------------------------------------------------

function getWallet(api) {
    return call0(api, "getWallet")
}

function createTopup(api, clubId, amount, promoCode) {
    if (promoCode && String(promoCode).length > 0 && has(api, "createTopupWithPromo")) {
        return call3(api, "createTopupWithPromo", clubId, amount, String(promoCode))
    }
    return call2(api, "createTopup", clubId, amount)
}

function pollIntent(api, publicId) {
    return call1(api, "pollIntent", publicId)
}

// Issues a fresh handoff URL for an existing pending intent. Needed once the
// original 120 s handoff code has expired; the response op is `handoff`.
function reissueHandoff(api, publicId) {
    return call1(api, "reissueHandoff", publicId)
}

function hasReissueHandoff(api) {
    return has(api, "reissueHandoff")
}

// ---- sessions -----------------------------------------------------------

function startSession(api, clubId) {
    return call1(api, "startSession", clubId)
}

function getCurrentSession(api) {
    return call0(api, "getCurrentSession")
}

function getSessionHistory(api) {
    return call0(api, "getSessionHistory")
}

function stopSession(api, id) {
    return call1(api, "stopSession", id)
}

function extendSession(api, id, amount) {
    return call2(api, "extendSession", id, amount)
}

function reconnectSession(api, id) {
    return call1(api, "reconnectSession", id)
}

// ---- queue --------------------------------------------------------------

function getQueue(api) {
    return call0(api, "getQueue")
}

function joinQueue(api, clubId) {
    return call1(api, "joinQueue", clubId)
}

function leaveQueue(api, id) {
    return call1(api, "leaveQueue", id)
}

function claimQueue(api, id) {
    return call1(api, "claimQueue", id)
}

// ---- shell --------------------------------------------------------------

function openExternal(api, url) {
    return call1(api, "openExternal", url)
}

function openInstallFolder(api) {
    return call0(api, "openInstallFolder")
}

function openVaultFolder(api) {
    return call0(api, "openVaultFolder")
}

function retryFailedStep(api) {
    return call0(api, "retryFailedStep")
}

function openLogFolder(api) {
    return call0(api, "openLogFolder")
}

function hasOpenLogFolder(api) {
    return has(api, "openLogFolder")
}

// Account deletion takes the handle plus the eight words as a list; the UI
// calls it only when present and otherwise points at support.
function hasDeleteAccount(api) {
    return has(api, "deleteAccount")
}

function deleteAccount(api, handle, words) {
    return call2(api, "deleteAccount", handle, words)
}

// ---- response data accessors -------------------------------------------

function clubsOf(data) {
    if (!data || !data.clubs) {
        return []
    }
    return data.clubs
}

function clubOf(data) {
    if (!data || !data.club) {
        return null
    }
    return data.club
}

function sessionOf(data) {
    if (!data || !data.session) {
        return null
    }
    return data.session
}

function entryOf(data) {
    if (!data || !data.entry) {
        return null
    }
    return data.entry
}

function walletClubsOf(data) {
    if (!data || !data.clubs) {
        return []
    }
    return data.clubs
}

function updatedAtOf(data) {
    if (!data || !data.updated_at) {
        return ""
    }
    return data.updated_at
}

function intentStatusOf(data) {
    if (!data || !data.status) {
        return ""
    }
    return data.status
}

// ---- backend properties -------------------------------------------------

// URL of a legal document by id (user_agreement, pdn_consent, ...).
// Bindings must pass a QML-level property as `documents` so the binding
// depends on it: reading api.legalDocuments inside a .pragma library function
// is invisible to the binding engine.
function documentUrlIn(documents, documentId) {
    if (!documents) {
        return ""
    }
    for (var i = 0; i < documents.length; i++) {
        if (String(documents[i].document_id) === String(documentId) && documents[i].url) {
            return String(documents[i].url)
        }
    }
    return ""
}

// ---- catalog helpers ----------------------------------------------------

function clubById(clubs, id) {
    if (!clubs) {
        return null
    }
    for (var i = 0; i < clubs.length; i++) {
        if (String(clubs[i].id) === String(id)) {
            return clubs[i]
        }
    }
    return null
}

function clubName(clubs, id) {
    var club = clubById(clubs, id)
    return club && club.name ? club.name : ""
}

function walletFor(walletClubs, id) {
    if (!walletClubs) {
        return null
    }
    for (var i = 0; i < walletClubs.length; i++) {
        if (String(walletClubs[i].club_id) === String(id)) {
            return walletClubs[i]
        }
    }
    return null
}

function balanceFor(walletClubs, id) {
    var row = walletFor(walletClubs, id)
    return row ? Number(row.balance) : 0
}

function totalBalance(walletClubs) {
    var total = 0
    if (!walletClubs) {
        return total
    }
    for (var i = 0; i < walletClubs.length; i++) {
        total += Number(walletClubs[i].balance || 0)
    }
    return total
}

function sortClubs(clubs) {
    if (!clubs) {
        return []
    }
    var copy = clubs.slice(0)
    copy.sort(function (a, b) {
        var aOpen = (a.status === "available" && Number(a.free_units) > 0) ? 0 : 1
        var bOpen = (b.status === "available" && Number(b.free_units) > 0) ? 0 : 1
        if (aOpen !== bOpen) {
            return aOpen - bOpen
        }
        return Number(a.price_per_hour || 0) - Number(b.price_per_hour || 0)
    })
    return copy
}

// "Свободно 3 из 5" / "Занято" / "Обслуживание" / "Закрыто"
function clubAvailabilityText(club) {
    if (!club) {
        return ""
    }
    if (club.schedule && club.schedule.closed === true) {
        return "Закрыто"
    }
    if (club.status === "maintenance") {
        return "Обслуживание"
    }
    if (club.status === "offline") {
        return "Недоступен"
    }
    var free = Number(club.free_units || 0)
    var total = Number(club.total_units || 0)
    if (free <= 0) {
        return "Занято"
    }
    return "Свободно " + free + " из " + total
}

function clubTone(club) {
    if (!club) {
        return "neutral"
    }
    if (club.schedule && club.schedule.closed === true) {
        return "neutral"
    }
    if (club.status === "maintenance" || club.status === "offline") {
        return "neutral"
    }
    if (Number(club.free_units || 0) <= 0) {
        return "bad"
    }
    if (Number(club.free_units || 0) === 1) {
        return "warn"
    }
    return "good"
}

function isClubOpen(club) {
    if (!club) {
        return false
    }
    if (club.schedule && club.schedule.closed === true) {
        return false
    }
    return club.status === "available"
}

function traceReason(club, stale, balance) {
    if (stale) {
        return "Данные устарели. Обновите список."
    }
    if (!club) {
        return "Клуб не выбран."
    }
    if (club.schedule && club.schedule.closed === true) {
        return "Клуб закрыт по расписанию."
    }
    if (club.status === "maintenance") {
        return "Клуб на обслуживании."
    }
    if (club.status === "offline") {
        return "Клуб недоступен."
    }
    if (Number(club.free_units || 0) <= 0) {
        return "Свободных компьютеров нет. Встаньте в очередь."
    }
    var minutePrice = Number(club.price_per_minute || 0)
    if (Number(balance || 0) <= 0 ||
            (minutePrice > 0 && Number(balance || 0) < minutePrice)) {
        return "Недостаточно средств. Пополните баланс."
    }
    return ""
}

// ---- session and queue copy --------------------------------------------

function sessionStatusText(status) {
    switch (status) {
    case "pending":
        return "Готовим компьютер"
    case "active":
        return "Сессия активна"
    case "disconnected":
        return "Соединение прервано"
    case "expired":
        return "Сессия завершена"
    case "stopped":
        return "Сессия завершена"
    default:
        return "Нет активной сессии"
    }
}

function sessionTone(status) {
    switch (status) {
    case "active":
        return "good"
    case "pending":
        return "warn"
    case "disconnected":
        return "warn"
    case "expired":
        return "neutral"
    default:
        return "neutral"
    }
}

function queueStatusText(entry) {
    if (!entry) {
        return "Вы не в очереди"
    }
    switch (entry.status) {
    case "waiting":
        return "Ожидание в очереди"
    case "notified":
        return "Слот готов"
    case "claimed":
        return "Слот получен"
    case "expired":
        return "Время на подключение истекло"
    case "left":
        return "Вы вышли из очереди"
    default:
        return "Очередь"
    }
}

function queueTone(entry) {
    if (!entry) {
        return "neutral"
    }
    return entry.status === "notified" ? "accent" : "warn"
}

// ---- payments -----------------------------------------------------------

function intentStatusText(status) {
    switch (status) {
    case "pending":
        return "Платёж обрабатывается"
    case "succeeded":
        return "Баланс пополнен"
    case "failed":
        return "Платёж не прошёл"
    case "canceled":
        return "Платёж отменён"
    case "refunded":
        return "Возврат выполнен"
    default:
        return "Ожидаем оплату"
    }
}

function intentTone(status) {
    switch (status) {
    case "succeeded":
        return "good"
    case "failed":
        return "bad"
    case "canceled":
        return "neutral"
    case "refunded":
        return "warn"
    case "pending":
        return "warn"
    default:
        return "neutral"
    }
}

function isFinalIntentStatus(status) {
    return status === "succeeded" || status === "failed" ||
           status === "canceled" || status === "refunded"
}

function remainingSecondsOf(session) {
    if (!session) {
        return 0
    }
    return Math.max(0, Math.floor(Number(session.remaining_seconds || 0)))
}

// ---- error copy ---------------------------------------------------------
// Every code from notes/build-interfaces.md plus the Contract v1.4 additions.
// The strings are user-safe; the raw code is never shown as the only text.

function errorText(code, message) {
    var fallback = "Не удалось выполнить действие. Попробуйте ещё раз."
    var text = ""
    switch (code) {
    case "unauthenticated":
        text = "Сессия истекла. Войдите снова."
        break
    case "session_revoked":
        text = "Сессия отозвана. Войдите снова."
        break
    case "device_revoked":
        text = "Устройство отозвано. Восстановите доступ."
        break
    case "consent_required":
        text = "Нужно принять документы, чтобы продолжить."
        break
    case "consent_version_mismatch":
        text = "Документы обновились. Примите новую версию."
        break
    case "client_update_required":
        text = "Обновите приложение, чтобы продолжить."
        break
    case "club_available":
        text = "Клуб уже недоступен. Выберите другой."
        break
    case "session_limit_reached":
        text = "У вас уже есть активная сессия."
        break
    case "unsupported_capability":
        text = "Это действие пока недоступно."
        break
    case "payment_pending":
        text = "Платёж ещё обрабатывается. Проверьте статус позже."
        break
    case "validation_error":
        text = "Проверьте введённые данные."
        break
    case "rate_limited":
        text = "Слишком много запросов. Подождите немного."
        break
    case "offline":
        text = "Нет соединения. Проверьте интернет."
        break
    case "proof_invalid":
        text = "Не удалось подтвердить устройство. Попробуйте ещё раз."
        break
    case "control_required":
        text = "Для этого действия нужны слова восстановления."
        break
    case "recovery_ticket_invalid":
        text = "Ссылка восстановления недействительна. Начните заново."
        break
    case "consent_required_for_payment":
        text = "Документы обновились. Примите их в приложении."
        break
    case "not_found":
        text = "Не найдено. Обновите список."
        break
    case "invalid_or_expired_challenge":
        text = "Проверка устарела. Начните заново."
        break
    case "unknown_device":
        text = "Это устройство не зарегистрировано. Восстановите доступ."
        break
    case "device_stale":
        text = "Устройство устарело. Восстановите доступ."
        break
    case "consent_invalid":
        text = "Не удалось подтвердить согласие. Повторите регистрацию."
        break
    case "legal_unavailable":
        text = "Не удалось открыть документы. Проверьте соединение."
        break
    case "invalid_response":
        text = "Сервер вернул неожиданный ответ. Повторите попытку."
        break
    case "unexpected_redirect":
        text = "Сервер перенаправил запрос. Проверьте адрес API."
        break
    case "vault_unavailable":
        text = "Не удалось открыть хранилище ключей. Аккаунт недоступен."
        break
    case "device_key_corrupt":
        text = "Ключ устройства повреждён. Сбросьте локальные данные или восстановите доступ."
        break
    case "device_key_missing":
        text = "Ключ устройства не найден. Восстановите доступ по словам."
        break
    case "server_error":
        text = "Сервер не отвечает. Попробуйте ещё раз."
        break
    default:
        text = ""
        break
    }

    var result = fallback
    if (text.length > 0) {
        result = text
    }
    else if (message && String(message).length > 0) {
        result = String(message)
    }

    // Support and bug reports need the exact slug; the localized sentence
    // alone hides which server/client branch failed.
    var codeText = code ? String(code) : ""
    var messageText = message ? String(message) : ""
    if (codeText.length > 0 && result.indexOf(codeText) < 0 &&
            messageText.indexOf(codeText) < 0) {
        result = result + " [" + codeText + "]"
    }
    return result
}

// Error text with a short operation title so a banner never says just
// "Действие". The generic title is dropped.
function errorTextFor(op, code, message) {
    var text = errorText(code, message)
    var title = op ? opTitle(String(op)) : ""
    if (title.length > 0 && title !== "Действие") {
        return title + ": " + text
    }
    return text
}

// A short operation title for error banners: "Пополнение", "Сессия", ...
function opTitle(op) {
    switch (op) {
    case "clubs":
        return "Клубы"
    case "club":
        return "Клуб"
    case "wallet":
        return "Кошелёк"
    case "wallet_history":
        return "История пополнений"
    case "topup":
    case "intent":
        return "Пополнение"
    case "handoff":
        return "Оплата"
    case "session_start":
    case "session_current":
    case "session_stop":
    case "session_extend":
    case "session_reconnect":
        return "Сессия"
    case "queue":
    case "queue_join":
    case "queue_leave":
    case "queue_claim":
        return "Очередь"
    case "register":
    case "register_account":
        return "Создание аккаунта"
    case "login":
        return "Вход"
    case "recover":
        return "Восстановление"
    case "confirm_words":
        return "Набор восстановления"
    case "refresh_me":
        return "Аккаунт"
    case "account_delete":
        return "Удаление аккаунта"
    case "legal_documents":
        return "Документы"
    case "kit":
        return "Набор восстановления"
    case "open_logs":
        return "Логи"
    default:
        return "Действие"
    }
}
