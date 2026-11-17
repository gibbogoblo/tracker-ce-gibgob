#pragma once
struct FileManagerStub
{
    int stored = 0, undone = 0;
    void storePatternUndoRevision() { stored++; }
    void undoPattern() { undone++; }
};
extern FileManagerStub newFileManager;
