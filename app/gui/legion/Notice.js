.pragma library

// Notification policy shared by pages and the shell notification host.
// Presentation is deliberately data-driven: a network failure must not be
// rendered as an inline "club unavailable" error, and a business conflict
// must not become a modal popup.

// Storefront, API and legal-docs failures are one user-facing condition.
// The app does not split "front is down" from "API is down": if Django is
// unhealthy, the honest message is that Django is unavailable.
function isDjangoDown(code) {
    switch (code) {
    case "offline":
    case "network_error":
    case "timeout":
    case "server_error":
    case "internal_error":
    case "app_api_disabled":
    case "legal_unavailable":
    case "legal_documents_unavailable":
        return true
    default:
        return false
    }
}

function presentationFor(code) {
    if (isDjangoDown(code)) {
        return "banner"
    }
    switch (code) {
    case "rate_limited":
    case "client_update_required":
        return "banner"
    case "queue_deadline_expired":
        return "popup"
    case "vault_unavailable":
    case "device_key_corrupt":
    case "device_key_missing":
    case "device_revoked":
    case "device_stale":
    case "key_revoked":
    case "unknown_device":
    case "account_deletion_pending":
        return "fullPage"
    default:
        return "inline"
    }
}

function toneFor(code) {
    if (isDjangoDown(code)) {
        return code === "offline" || code === "network_error" || code === "timeout"
                ? "danger" : "warning"
    }
    switch (code) {
    case "rate_limited":
    case "queue_deadline_expired":
    case "club_full":
    case "no_available_pc":
    case "insufficient_balance":
    case "computer_offline":
    case "computer_waking":
    case "computer_searching":
        return "warning"
    case "queue_notified":
    case "success":
        return "success"
    default:
        return "danger"
    }
}

function forError(code, message) {
    var detail = message ? String(message) : ""
    if (isDjangoDown(code)) {
        return {
            code: "django_unavailable",
            tone: toneFor(code),
            title: "Django недоступен",
            text: "Сервер временно недоступен. Игра, оплата и документы могут не работать.",
            detail: detail,
            actionText: "Повторить",
            retryable: true,
            presentation: "banner"
        }
    }
    if (code === "rate_limited") {
        return {
            code: "rate_limited",
            tone: "warning",
            title: "Слишком много запросов",
            text: "Подождите немного и попробуйте снова.",
            detail: detail,
            actionText: "",
            retryable: false,
            presentation: "banner"
        }
    }
    if (code === "client_update_required") {
        return {
            code: "client_update_required",
            tone: "warning",
            title: "Нужно обновить приложение",
            text: "Эта версия больше не поддерживается. Скачайте обновление.",
            detail: detail,
            actionText: "",
            retryable: false,
            presentation: "banner"
        }
    }
    if (code === "queue_deadline_expired") {
        return {
            code: "queue_deadline_expired",
            tone: "warning",
            title: "Время вышло",
            text: "Слот больше не зарезервирован.",
            detail: detail,
            actionText: "К клубам",
            retryable: false,
            presentation: "popup"
        }
    }
    return {
        code: code ? String(code) : "unknown_error",
        tone: toneFor(code),
        title: "Не удалось выполнить действие",
        text: detail.length > 0 ? detail : "Попробуйте ещё раз.",
        detail: "",
        actionText: "",
        retryable: false,
        presentation: "inline"
    }
}

function forSuccess(title, text, detail) {
    return {
        code: "success",
        tone: "success",
        title: title ? String(title) : "Готово",
        text: text ? String(text) : "",
        detail: detail ? String(detail) : "",
        actionText: "",
        retryable: false,
        presentation: "banner",
        autoHide: 5000
    }
}
