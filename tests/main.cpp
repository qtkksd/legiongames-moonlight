// C4 — moonlight-mic QtTest runner.
// Runs all three test classes in sequence, writing each class's results to a
// separate output file so they can be read back by the verification script.

#include <QCoreApplication>
#include <QtTest>

extern int runTogglePersistenceTests(int argc, char** argv);
extern int runGateLogicTests(int argc, char** argv);
extern int runMicLifecycleTests(int argc, char** argv);
extern int runAccountCoreTests(int argc, char** argv);

// Build an argv array from a QStringList, valid for the call duration.
static void makeArgv(const QStringList& args,
                     QVector<QByteArray>& storage,
                     QVector<char*>&      ptrs)
{
    storage.clear();
    ptrs.clear();
    for (const QString& s : args) {
        storage.append(s.toUtf8());
        ptrs.append(storage.last().data());
    }
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int status = 0;

    // Test A — toggle persistence
    {
        QStringList args = {argv[0],
            "-o", "test-results-persistence.txt,txt",
            "-v2"};
        QVector<QByteArray> storage;
        QVector<char*> ptrs;
        makeArgv(args, storage, ptrs);
        int c = ptrs.size();
        status |= runTogglePersistenceTests(c, ptrs.data());
    }

    // Test B — gate logic
    {
        QStringList args = {argv[0],
            "-o", "test-results-gate.txt,txt",
            "-v2"};
        QVector<QByteArray> storage;
        QVector<char*> ptrs;
        makeArgv(args, storage, ptrs);
        int c = ptrs.size();
        status |= runGateLogicTests(c, ptrs.data());
    }

    // Test C — MicAudioSender lifecycle
    {
        QStringList args = {argv[0],
            "-o", "test-results-lifecycle.txt,txt",
            "-v2"};
        QVector<QByteArray> storage;
        QVector<char*> ptrs;
        makeArgv(args, storage, ptrs);
        int c = ptrs.size();
        status |= runMicLifecycleTests(c, ptrs.data());
    }

    // Test D — account core (vault, key, proof, consent, deep link, manifest)
    {
        QStringList args = {argv[0],
            "-o", "test-results-account.txt,txt",
            "-v2"};
        QVector<QByteArray> storage;
        QVector<char*> ptrs;
        makeArgv(args, storage, ptrs);
        int c = ptrs.size();
        status |= runAccountCoreTests(c, ptrs.data());
    }

    return status;
}
