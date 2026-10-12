#pragma once

#include <QtGlobal>

// End-of-booking notice state, handed over by LegionConnect at one-time-code
// time (/api/client/connect -> session_deadline) and consumed by the streaming
// Session to show in-stream overlay warnings.
//
// The client runs a purely local countdown off these values — there is no
// renewing token and no polling after the connect exchange.
//
//   T  = reservation/closing end ("end_of_booking")
//   T-30, T-15, T-5  → warnings before the reservation ends
//   T+grace-5        → final "hard stop soon" warning
//
// Users are only shown grace/2 as the "extra time" cushion.
namespace SessionDeadline {

inline qint64& endEpochMsRef()
{
    static qint64 value = 0;
    return value;
}

inline int& graceMinutesRef()
{
    static int value = 0;
    return value;
}

inline bool has()
{
    return endEpochMsRef() > 0;
}

inline void set(qint64 endEpochMs, int graceMinutes)
{
    endEpochMsRef() = endEpochMs;
    graceMinutesRef() = graceMinutes;
}

inline qint64 endEpochMs()
{
    return endEpochMsRef();
}

inline int graceMinutes()
{
    return graceMinutesRef();
}

inline void clear()
{
    endEpochMsRef() = 0;
    graceMinutesRef() = 0;
}

}
