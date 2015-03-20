#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "createdirectorydialog.h"
#include "progressdialog.h"
#include "mtpobjectsmodel.h"
#include <QDebug>
#include <QMessageBox>
#include <QKeyEvent>
#include <QFileDialog>
#include <QDir>

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	_ui(new Ui::MainWindow),
	_objectModel(new MtpObjectsModel)
{
	_ui->setupUi(this);
	connect(_ui->listView, SIGNAL(doubleClicked(QModelIndex)), SLOT(onActivated(QModelIndex)));
	connect(_ui->listView, SIGNAL(customContextMenuRequested(QPoint)), SLOT(customContextMenuRequested(QPoint)));
	connect(_ui->actionBack, SIGNAL(triggered()), SLOT(back()));
	connect(_ui->actionGo_Down, SIGNAL(triggered()), SLOT(down()));
	connect(_ui->actionCreateDirectory, SIGNAL(triggered()), SLOT(createDirectory()));
	connect(_ui->actionUploadDirectory, SIGNAL(triggered()), SLOT(uploadDirectories()));
	connect(_ui->actionUpload_Album, SIGNAL(triggered()), SLOT(uploadAlbum()));
	connect(_ui->actionUpload, SIGNAL(triggered()), SLOT(uploadFiles()));
}

MainWindow::~MainWindow()
{
	delete _ui;
}

void MainWindow::showEvent(QShowEvent *)
{
	if (!_device)
	{
		_device = mtp::Device::Find();
		if (!_device)
		{
			QMessageBox::critical(this, tr("MTP was not found"), tr("No MTP device found"));
			qFatal("device was not found");
		}
		_objectModel->setSession(_device->OpenSession(1));
		_ui->listView->setModel(_objectModel);
	}
}

void MainWindow::onActivated ( const QModelIndex & index )
{
	if (_objectModel->enter(index.row()))
		_history.push_back(_objectModel->parentObjectId());
}

void MainWindow::customContextMenuRequested ( const QPoint & pos )
{
	QItemSelectionModel *selection =_ui->listView->selectionModel();
	QModelIndexList rows = selection->selectedRows();

	QMenu menu(this);
	//http://standards.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
	QAction * delete_objects = menu.addAction(QIcon::fromTheme("edit-delete"), "Delete");
	QAction * action = menu.exec(_ui->listView->mapToGlobal(pos));
	if (!action)
		return;

	for(int i = rows.size() - 1; i >= 0; --i)
	{
		if (action == delete_objects)
			_objectModel->removeRow(rows[i].row());
		else
			qDebug() << "unknown action!";
	}
}

void MainWindow::back()
{
	if (_history.empty())
		return;
	_history.pop_back();
	mtp::u32 oid = _history.empty()? mtp::Session::Root: _history.back();
	_objectModel->setParent(oid);
}

void MainWindow::down()
{
	if (_objectModel->enter(_ui->listView->currentIndex().row()))
		_history.push_back(_objectModel->parentObjectId());
}

void MainWindow::createDirectory()
{
	CreateDirectoryDialog d(this);
	if (d.exec() && !d.name().isEmpty())
		_objectModel->createDirectory(d.name());
}

void MainWindow::uploadFiles(const QStringList &files)
{
	ProgressDialog progressDialog(this);
	progressDialog.setModal(true);
	progressDialog.setMaximum(files.size());
	progressDialog.setValue(0);
	progressDialog.show();
	int progress = 0;
	for(QString file : files)
	{
		progressDialog.setValue(++progress);
		QCoreApplication::processEvents();
		_objectModel->uploadFile(file);
	}
}

void MainWindow::uploadFiles()
{
	QFileDialog d(this);
	d.setAcceptMode(QFileDialog::AcceptOpen);
	d.setFileMode(QFileDialog::ExistingFiles);
	d.setOption(QFileDialog::ShowDirsOnly, false);
	d.exec();

	uploadFiles(d.selectedFiles());
}

void MainWindow::uploadDirectories()
{
	/*
	QFileDialog d(this);
	d.setAcceptMode(QFileDialog::AcceptOpen);
	d.setFileMode(QFileDialog::Directory);
	d.setOption(QFileDialog::ShowDirsOnly, true);
	d.exec();
*/
	QString dirPath = QFileDialog::getExistingDirectory(this);
	if (dirPath.isEmpty())
		return;

	QDir dir(dirPath);
	qDebug() << "adding directory " << dir.dirName();
	mtp::u32 dirId = _objectModel->createDirectory(dir.dirName());
	_objectModel->setParent(dirId);
	_history.push_back(dirId);

	QStringList files;
	for(QString file : dir.entryList(QDir::Files))
	{
		files.push_back(dir.canonicalPath() + "/" + file);
	}
	uploadFiles(files);
}

void MainWindow::uploadAlbum()
{
	QString dirPath = QFileDialog::getExistingDirectory(this);
	if (!dirPath.isEmpty())
		uploadAlbum(dirPath);
}

namespace
{
	int GetScore(const QString &str_)
	{
		QString str = str_.toLower();
		int score = 0;
		if (str.contains("art"))
			score += 1;
		if (str.contains("album"))
			score += 1;
		if (str.contains("large"))
			score += 2;
		if (str.contains("small"))
			score += 1;
		if (str.contains("folder"))
			score += 1;
		return score;
	}

	bool HeuristicLess(const QString &s1, const QString &s2)
	{
		return GetScore(s1) > GetScore(s2);
	}
}

void MainWindow::uploadAlbum(QString dirPath)
{
	QDir dir(dirPath);
	qDebug() << "adding directory " << dir.dirName();

	mtp::u32 dirId = _objectModel->createDirectory(dir.dirName());
	_objectModel->setParent(dirId);
	_history.push_back(dirId);

	QString cover, coverTarget;
	QStringList covers;
	{
		QStringList ext({"*.png", "*.jpg", "*.jpeg"});
		covers = dir.entryList(ext, QDir::Files);
		qSort(covers.begin(), covers.end(), &HeuristicLess);
		qDebug() << "covers" << covers;
		if (!covers.isEmpty())
			cover = covers.front();

		if (cover.endsWith(".png"))
			coverTarget = "albumart.png";
		else
			coverTarget = "albumart.jpg";

		qDebug() << "use " << cover << " as album art -> " << coverTarget;
	}

	_objectModel->uploadFile(dir.canonicalPath() + "/" + cover, coverTarget);

	QStringList files;
	for(QString file : dir.entryList(QDir::Files))
	{
		if (!covers.contains(file))
			files.push_back(dir.canonicalPath() + "/" + file);
	}
	uploadFiles(files);
}
