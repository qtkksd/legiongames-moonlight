import QtQuick 2.9

import "Api.js" as Api

// Single owner for shared server reads. Views consume LegionShell aliases;
// this object owns cache keys, TTLs, in-flight guards and mutation ordering.
QtObject {
    id: store

    property var api: null

    property var session: null
    property var queueEntry: null
    property var wallet: []
    property var catalog: []
    property string catalogAt: ""
    property bool catalogLoading: false
    property string catalogErrorCode: ""
    property string catalogErrorMessage: ""

    readonly property int catalogCacheTtlMs: 5000
    readonly property int clubCacheTtlMs: 10000
    readonly property int walletCacheTtlMs: 10000
    readonly property int sessionCacheTtlMs: 5000
    readonly property int queueCacheTtlMs: 3000
    readonly property int sessionHistoryCacheTtlMs: 30000
    property int queuePollInterval: 5000

    property double catalogFetchedAt: 0
    property var clubFetchedAt: ({})
    property var clubInFlight: ({})
    property var clubDirty: ({})
    property double walletFetchedAt: 0
    property double sessionFetchedAt: 0
    property double queueFetchedAt: 0
    property var sessionHistory: []
    property double sessionHistoryFetchedAt: 0
    property bool sessionHistoryLoading: false
    property bool sessionHistoryInFlight: false
    property string sessionHistoryError: ""
    property bool catalogInFlight: false
    property bool walletInFlight: false
    property bool sessionInFlight: false
    property bool queueInFlight: false
    property bool catalogDirty: false
    property bool walletDirty: false
    property bool sessionDirty: false
    property bool queueDirty: false

    property int sessionMutationSeq: 0
    property int sessionReadMutationSeq: 0
    property int queueMutationSeq: 0
    property int queueReadMutationSeq: 0

    function loadCatalog(availableOnly, force) {
        if (catalogInFlight) {
            if (force) {
                catalogDirty = true
            }
            return false
        }
        if (!force && catalogFetchedAt > 0 &&
                (Date.now() - catalogFetchedAt) < catalogCacheTtlMs) {
            return false
        }
        catalogInFlight = true
        catalogLoading = true
        catalogErrorCode = ""
        catalogErrorMessage = ""
        if (!Api.getClubs(api, false)) {
            catalogInFlight = false
            catalogLoading = false
            catalogErrorCode = "offline"
            return false
        }
        return true
    }

    function clubFetchedAtMs(slug) {
        if (!slug) {
            return 0
        }
        var value = clubFetchedAt[String(slug)]
        return value ? Number(value) : 0
    }

    function refreshClub(slug, force) {
        var key = slug ? String(slug).trim() : ""
        if (key.length === 0) {
            return false
        }
        if (clubInFlight[key] === true) {
            if (force) {
                clubDirty[key] = true
            }
            return false
        }
        var fetchedAt = clubFetchedAtMs(key)
        if (!force && fetchedAt > 0 &&
                (Date.now() - fetchedAt) < clubCacheTtlMs) {
            return false
        }
        clubInFlight[key] = true
        if (!Api.getClub(api, key)) {
            clubInFlight[key] = false
            return false
        }
        return true
    }

    function refreshWallet(force) {
        if (walletInFlight) {
            if (force) {
                walletDirty = true
            }
            return false
        }
        if (!force && walletFetchedAt > 0 &&
                (Date.now() - walletFetchedAt) < walletCacheTtlMs) {
            return false
        }
        walletInFlight = true
        if (!Api.getWallet(api)) {
            walletInFlight = false
            return false
        }
        return true
    }

    function refreshWalletIfStale() {
        return refreshWallet(false)
    }

    function refreshSession(force) {
        if (sessionInFlight) {
            if (force) {
                sessionDirty = true
            }
            return false
        }
        if (!force && sessionFetchedAt > 0 &&
                (Date.now() - sessionFetchedAt) < sessionCacheTtlMs) {
            return false
        }
        sessionReadMutationSeq = sessionMutationSeq
        sessionInFlight = true
        if (!Api.getCurrentSession(api)) {
            sessionInFlight = false
            return false
        }
        return true
    }

    function refreshQueue(force) {
        if (queueInFlight) {
            if (force) {
                queueDirty = true
            }
            return false
        }
        if (!force && queueFetchedAt > 0 &&
                (Date.now() - queueFetchedAt) < queueCacheTtlMs) {
            return false
        }
        queueReadMutationSeq = queueMutationSeq
        queueInFlight = true
        if (!Api.getQueue(api)) {
            queueInFlight = false
            return false
        }
        return true
    }

    function refreshSessionHistory(force) {
        if (sessionHistoryInFlight) {
            return false
        }
        if (!force && sessionHistoryFetchedAt > 0 &&
                (Date.now() - sessionHistoryFetchedAt) < sessionHistoryCacheTtlMs) {
            return false
        }
        sessionHistoryInFlight = true
        sessionHistoryLoading = true
        sessionHistoryError = ""
        if (!Api.getSessionHistory(api)) {
            sessionHistoryInFlight = false
            sessionHistoryLoading = false
            sessionHistoryError = "offline"
            return false
        }
        return true
    }

    function refreshAll(force) {
        loadCatalog(false, force === true)
        refreshWallet(force === true)
        refreshSession(force === true)
        refreshQueue(force === true)
    }

    function updateClub(updated) {
        if (!updated || updated.id === undefined) {
            return
        }
        var next = []
        var replaced = false
        for (var i = 0; i < catalog.length; i++) {
            if (String(catalog[i].id) === String(updated.id)) {
                next.push(updated)
                replaced = true
            }
            else {
                next.push(catalog[i])
            }
        }
        if (!replaced) {
            next.push(updated)
        }
        catalog = next
    }

    function clubById(id) {
        return Api.clubById(catalog, id)
    }

    function clubName(id) {
        return Api.clubName(catalog, id)
    }

    function walletFor(clubId) {
        return Api.walletFor(wallet, clubId)
    }

    function balanceFor(clubId) {
        return Api.balanceFor(wallet, clubId)
    }
}
