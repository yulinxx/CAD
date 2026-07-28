#pragma once

#include <QString>
#include <memory>
#include <string>

class OperationBus;
class UiStateCenter;
class LayerPersistenceBridge;
class PersistenceService;
class ImportService;
class ExportService;
class FileDialogService;
class RecentFileService;
class HelpDialogService;

namespace Eg
{
    class SceneManager;
}

class QWidget;

class FileOperationRegistry
{
public:
    FileOperationRegistry(OperationBus* bus,
        Eg::SceneManager* sceneManager,
        ImportService* importService,
        ExportService* exportService,
        FileDialogService* fileDialog,
        RecentFileService* recentFiles,
        HelpDialogService* helpDialog,
        UiStateCenter* stateCenter,
        LayerPersistenceBridge* layerPersistence,
        PersistenceService* persistence,
        QWidget* parentWidget);

    void registerAll();

private:
    OperationBus* m_bus;
    Eg::SceneManager* m_sceneManager;
    ImportService* m_importService;
    ExportService* m_exportService;
    FileDialogService* m_fileDialog;
    RecentFileService* m_recentFiles;
    HelpDialogService* m_helpDialog;
    UiStateCenter* m_stateCenter;
    LayerPersistenceBridge* m_layerPersistence;
    PersistenceService* m_persistence;
    QWidget* m_parentWidget;
};
