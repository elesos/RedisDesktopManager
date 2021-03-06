#include "viewmodel.h"
#include "connections-tree/items/keyitem.h"
#include "value-editor/valueviewmodel.h"
#include "redisclient/connection.h"
#include <QDebug>

ValueEditor::ViewModel::ViewModel(QSharedPointer<AbstractKeyFactory> keyFactory)
    : m_keyFactory(keyFactory), m_currentTabIndex(0)
{
}

void ValueEditor::ViewModel::openTab(QSharedPointer<RedisClient::Connection> connection,
                                     ConnectionsTree::KeyItem& key, bool inNewTab)
{
    m_keyFactory->loadKey(connection, key.getFullPath(), key.getDbIndex(),
                        [this, inNewTab, &key](QSharedPointer<Model> keyModel)
    {
        if (keyModel.isNull())
            return;

        loadModel(keyModel, inNewTab);

        QObject::connect(keyModel.data(), &Model::removed,
                         this, [this, keyModel, &key]()
        {
            removeModel(keyModel);
            key.setRemoved(); //Disable key in connections tree
        });

        QObject::connect(&key, &ConnectionsTree::KeyItem::destroyed,
                         this, [this, keyModel] () {
            removeModel(keyModel);
        });
    });
    // TODO: add empty key model for loading
}


QModelIndex ValueEditor::ViewModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(column);
    Q_UNUSED(parent);
    return createIndex(row, 0);
}

int ValueEditor::ViewModel::rowCount(const QModelIndex&) const
{
    return m_valueModels.count();
}

QVariant ValueEditor::ViewModel::data(const QModelIndex &index, int role) const
{
    if (!isIndexValid(index))
        return QVariant();

    QSharedPointer<Model> model = m_valueModels.at(index.row());

    if (model.isNull())
        return QVariant();

    switch (role) {
        case keyIndex: return index.row();
        case keyNameRole: return model->getKeyName();
        case keyTTL: return model->getTTL();
        case keyType: return model->getType();
        case state: return model->getState();
        case showValueNavigation: return model->isMultiRow();
        case columnNames: return QVariant(model->getColumnNames()).toList();
        case count: return static_cast<qulonglong>(model->rowsCount());
    }

    return QVariant();
}

QHash<int, QByteArray> ValueEditor::ViewModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[keyIndex] = "keyIndex";
    roles[keyNameRole] = "keyName";
    roles[keyTTL] = "keyTtl";
    roles[keyType] = "keyType";
    roles[state] = "keyState";
    roles[showValueNavigation] = "showValueNavigation";
    roles[columnNames] = "columnNames";
    roles[count] = "valuesCount";
    roles[keyValue] = "keyValue";
    return roles;
}

void ValueEditor::ViewModel::addKey(QString keyName, QString keyType, const QVariantMap &row)
{
    m_keyFactory->addKey(m_newKeyRequest.first,
                         keyName, m_newKeyRequest.second,
                         keyType, row);
}

void ValueEditor::ViewModel::renameKey(int i, const QString& newKeyName)
{
    if (!isIndexValid(index(i, 0)))
        return;

    qDebug() << "Rename key:" << newKeyName;

    auto value = m_valueModels.at(i);

    try {
        value->setKeyName(newKeyName);
        emit dataChanged(index(i, 0), index(i, 0));
    } catch (const Model::Exception& e) {
        emit keyError(i, "Can't rename key: " + QString(e.what()));
    }
}

void ValueEditor::ViewModel::removeKey(int i)
{
    if (!isIndexValid(index(i, 0)))
        return;

    qDebug() << "Remove key:" << i;

    auto value = m_valueModels.at(i);

    try {
        value->removeKey();
    } catch (const Model::Exception& e) {
        emit keyError(i, "Can't remove key: " + QString(e.what()));
    }
}

void ValueEditor::ViewModel::closeTab(int i)
{
    if (!isIndexValid(index(i, 0)))
        return;

    try {
        beginRemoveRows(QModelIndex(), i, i);
        m_valueModels.removeAt(i);
        endRemoveRows();
    } catch (const Model::Exception& e) {
        emit keyError(i, "Can't remove key: " + QString(e.what()));
    }
}

void ValueEditor::ViewModel::setCurrentTab(int i)
{
    m_currentTabIndex = i;
}

QObject* ValueEditor::ViewModel::getValue(int i)
{
    if (!isIndexValid(index(i, 0)))
        return nullptr;

    auto model = m_valueModels.at(i);

    QList<QObject *> valueEditors = model->findChildren<QObject *>();

    qDebug() << "value editors:" << valueEditors.size();

    if (valueEditors.isEmpty())
        return new ValueEditor::ValueViewModel(model);
    else
        return valueEditors[0];
}

void ValueEditor::ViewModel::openNewKeyDialog(QSharedPointer<RedisClient::Connection> connection,
                                              int dbIndex, QString keyPrefix)
{
    if (connection.isNull() || dbIndex < 0)
        return;

    m_newKeyRequest = qMakePair(connection, dbIndex);

    QString dbId= QString("%1:db%2")
            .arg(connection->getConfig().name())
            .arg(dbIndex);

    emit newKeyDialog(dbId, keyPrefix);
}

bool ValueEditor::ViewModel::isIndexValid(const QModelIndex &index) const
{
    return 0 <= index.row() && index.row() < rowCount();
}

void ValueEditor::ViewModel::loadModel(QSharedPointer<ValueEditor::Model> model, bool openNewTab)
{
    if (m_valueModels.count() == 0)
        emit closeWelcomeTab();

    if (openNewTab || m_valueModels.count() == 0) {
        beginInsertRows(QModelIndex(), m_valueModels.count(), m_valueModels.count());
        m_valueModels.append(model);
        endInsertRows();
    } else {
        m_valueModels.insert(m_currentTabIndex, model);
        m_valueModels.removeAt(m_currentTabIndex+1);
        emit replaceTab(m_currentTabIndex);
        emit dataChanged(index(m_currentTabIndex, 0), index(m_currentTabIndex, 0));
    }
}

void ValueEditor::ViewModel::removeModel(QSharedPointer<ValueEditor::Model> model)
{
    int i = m_valueModels.lastIndexOf(model);

    if (i == -1) {
        qDebug() << "[Remove model] Key model not found!";
        return;
    }

    beginRemoveRows(QModelIndex(), i, i);
    m_valueModels.removeAt(i);
    endRemoveRows();
}
