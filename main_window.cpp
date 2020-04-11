/*
 * Laigter: an automatic map generator for lighting effects.
 * Copyright (C) 2019  Pablo Ivan Fonovich
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * Contact: azagaya.games@gmail.com
 */

#include "main_window.h"
#include "gui/about_dialog.h"
#include "gui/frame_splitter.h"
#include "gui/language_selector.h"
#include "gui/nb_selector.h"
#include "gui/presets_manager.h"
#include "gui/remove_plugin_dialog.h"
#include "src/brush_interface.h"
#include "src/open_gl_widget.h"
#include "ui_main_window.h"

#include <QColorDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QJsonObject>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPluginLoader>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);

  /* Language settings */
  el = new LanguageSelector(this);

  QString current_language =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/lang";
  QFile l(current_language);
  QTranslator *translator = new QTranslator;
  el->translator = translator;
  if (l.open(QIODevice::ReadOnly))
  {
    QTextStream in(&l);
    QStringList locale = in.readLine().split("\t");
    translator->load(":/translations/laigter_" + locale[2]);
    el->icon =
        QPixmap::fromImage(QImage(":/translations/flags/" + locale[1]));
  }
  else
  {
    bool loaded =
        translator->load(QLocale::system(), ":/translations/laigter", "_");
    if (!loaded)
    {
      translator->load(":/translations/laigter_en");
      el->icon =
          QPixmap::fromImage(QImage(":/translations/flags/EN.png"));
    }
    else
    {
      /* Get icon of locale language */
      QFile f(":/translations/languages.txt");
      f.open(QIODevice::ReadOnly);
      QTextStream stream(&f);
      QString locale = QLocale::system().bcp47Name();
      while (!stream.atEnd())
      {
        QStringList line = stream.readLine().split("\t");
        if (line.count() >= 3)
        {
          QString icon_path = line[1];
          QString lang = line[2];
          if (lang == locale || lang == locale.split("-")[0])
          {
            el->icon = QPixmap::fromImage(
                QImage(":/translations/flags/" + icon_path));
          }
        }
      }
    }
  }

  qApp->installTranslator(translator);

  project.processorList = &processorList;
  sample_processor = new ImageProcessor();
  processor = sample_processor;
  ui->openGLPreviewWidget->sampleLightList =
      sample_processor->get_light_list_ptr();

  QColor c;
  c.setRgbF(0.0, 1.0, 0.7);
  currentColor = c;
  currentAmbientcolor = QColor("white");
  currentSpecColor = c;
  c.setRgbF(0.2, 0.2, 0.3);
  currentBackgroundColor = c;
  currentSpecBaseColor = c;

  QPixmap pixmap(100, 100);
  pixmap.fill(currentColor);
  ui->pushButtonColor->setIcon(QIcon(pixmap));
  pixmap.fill(currentAmbientcolor);
  ui->pushButtonAmbientColor->setIcon(QIcon(pixmap));
  pixmap.fill(currentBackgroundColor);
  ui->pushButtonBackgroundColor->setIcon(QIcon(pixmap));

  ui->listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->listWidget, SIGNAL(customContextMenuRequested(const QPoint &)),
          SLOT(showContextMenuForListWidget(const QPoint &)));
  connect(ui->sliderParallax, SIGNAL(valueChanged(int)),
          ui->openGLPreviewWidget, SLOT(setParallaxHeight(int)));
  connect(ui->checkBoxPixelated, SIGNAL(toggled(bool)),
          ui->openGLPreviewWidget, SLOT(setPixelated(bool)));
  connect(ui->checkBoxToon, SIGNAL(toggled(bool)), ui->openGLPreviewWidget,
          SLOT(setToon(bool)));
  connect(ui->openGLPreviewWidget,
          SIGNAL(selectedLightChanged(LightSource *)), this,
          SLOT(selectedLightChanged(LightSource *)));
  connect(ui->openGLPreviewWidget, SIGNAL(stopAddingLight()), this,
          SLOT(stopAddingLight()));
  connect(ui->openGLPreviewWidget, SIGNAL(set_enabled_map_controls(bool)),
          this, SLOT(set_enabled_map_controls(bool)));
  connect(ui->openGLPreviewWidget, SIGNAL(set_enabled_light_controls(bool)),
          this, SLOT(set_enabled_light_controls(bool)));

  tabifyDockWidget(ui->normalDockWidget, ui->specularDockWidget);
  tabifyDockWidget(ui->normalDockWidget, ui->parallaxDockWidget);
  tabifyDockWidget(ui->normalDockWidget, ui->occlusionDockWidget);
  tabifyDockWidget(ui->dockWidgetTextures, ui->dockWidgetExport);

  ui->dockWidgetTextures->raise();
  ui->normalDockWidget->raise();
  ui->parallaxQuantizationSlider->setVisible(false);
  ui->labelQuantization->setVisible(false);
  ui->sliderParallaxBright->setVisible(false);
  ui->sliderParallaxContrast->setVisible(false);
  ui->labelBrightness->setVisible(false);
  ui->labelContrast->setVisible(false);

  QSettings settings("Azagaya", "Laigter");
  restoreGeometry(settings.value("geometry").toByteArray());
  restoreState(settings.value("windowState").toByteArray());
  setAcceptDrops(true);

  ui->listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui->listWidget->setDragDropMode(QAbstractItemView::InternalMove);

  connect(ui->openGLPreviewWidget,
          SIGNAL(processor_selected(ImageProcessor *, bool)), this,
          SLOT(processor_selected(ImageProcessor *, bool)));
  connect(ui->openGLPreviewWidget, SIGNAL(initialized()), this,
          SLOT(openGL_initialized()));
  connect(&fs_watcher, SIGNAL(fileChanged(QString)), this,
          SLOT(onFileChanged(QString)));
  set_enabled_light_controls(false);

  // Setting style
  QFile stylesheet_file(":/styles/classic.qss");
  stylesheet_file.open(QFile::ReadOnly);
  QString stylesheet = QLatin1String(stylesheet_file.readAll());
  qApp->setStyleSheet(stylesheet);
}

void MainWindow::showContextMenuForListWidget(const QPoint &pos)
{
  if (ui->listWidget->selectedItems().count() == 0)
    return;

  current_item = ui->listWidget->itemAt(pos);
  QMenu contextMenu(tr("Context menu"), ui->listWidget);
  contextMenu.addAction(new QAction(tr("Remove"), ui->listWidget));
  contextMenu.addSeparator();
  contextMenu.addAction(new QAction(tr("Load heightmap")));
  contextMenu.addAction(new QAction(tr("Reset heightmap")));
  contextMenu.addSeparator();
  contextMenu.addAction(new QAction(tr("Load specular map")));
  contextMenu.addAction(new QAction(tr("Reset specular map")));
  contextMenu.addSeparator();
  contextMenu.addAction(new QAction(tr("Add new frames")));
  ImageProcessor *p = find_processor(current_item->text());

  if (p->frames.count() > 1)
  {
    QAction *removeFrame = new QAction(tr("Remove current frame"));
    QAction *nextFrame = new QAction(tr("Next Frame"));
    QAction *prevFrame = new QAction(tr("Previous Frame"));
    contextMenu.addAction(removeFrame);
    if (p->animation.isActive())
    {
      contextMenu.addAction(new QAction(tr("Stop Animation")));
      removeFrame->setEnabled(false);
      nextFrame->setEnabled(false);
      prevFrame->setEnabled(false);
    }
    else
      contextMenu.addAction(new QAction(tr("Start Animation")));

    if (p->get_current_frame_id() == p->frames.count() - 1)
      nextFrame->setEnabled(false);
    else if (p->get_current_frame_id() == 0)
      prevFrame->setEnabled(false);

    contextMenu.addAction(nextFrame);
    contextMenu.addAction(prevFrame);
  } else {
    contextMenu.addAction(new QAction(tr("Split in frames")));
  }

  connect(&contextMenu, SIGNAL(triggered(QAction *)), this,
          SLOT(list_menu_action_triggered(QAction *)));
  contextMenu.exec(ui->listWidget->mapToGlobal(pos));
  disconnect(&contextMenu, SIGNAL(triggered(QAction *)), this,
             SLOT(list_menu_action_triggered(QAction *)));
}

void MainWindow::remove_processor(ImageProcessor *p){
  QStringList paths;

  for (int i=0; i<p->frames.count(); i++){
    Sprite frame;
    frame = p->frames[i];
    paths.append(frame.fileName);
    paths.append(frame.heightmapPath);
    paths.append(frame.specularPath);
    for (int j = 0; j < 3; j++){
      for (int k = 0; k < 3; k++){
        paths.append(frame.neighours_paths[j][k]);
      }
    }
  }

  fs_watcher.removePaths(paths);

  for (int i = 0; i < ui->listWidget->count(); i++){
    QListWidgetItem *item = ui->listWidget->item(i);
    if (p->get_name() == item->text()){
      delete item;
      break;
    }
  }

  processorList.removeOne(p);
  p->deleteLater();

}

void MainWindow::list_menu_action_triggered(QAction *action)
{
  ImageProcessor *p = find_processor(current_item->text());
  QString option = action->text();
  if (option == tr("Remove"))
  {
    remove_processor(p);

    if (ui->listWidget->selectedItems().count() == 0)
    {
      if (ui->listWidget->count() == 0)
      {
        ui->openGLPreviewWidget->clear_processor_list();
        processor_selected(sample_processor, true);
        ui->openGLPreviewWidget->add_processor(sample_processor);
      }
      else
      {
        ui->listWidget->setCurrentRow(0);
      }
    }
  }
  else if (option == tr("Load heightmap"))
  {
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open Image"), "",
        tr("Image File (*.png *.jpg *.bmp *.tga)"));
    if (fileName != nullptr)
    {
      bool success;
      QImage height = il.loadImage(fileName, &success);
      if (!success)
        return;

      fs_watcher.addPath(fileName);
      height =
          height.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
      p->loadHeightMap(fileName, height);
    }
  }
  else if (option == tr("Reset heightmap"))
  {
    bool succes;
    fs_watcher.removePath(p->get_heightmap_path());
    QImage height = il.loadImage(p->get_name(), &succes);
    height = height.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    p->loadHeightMap(p->get_name(), height);
  }
  else if (option == tr("Load specular map"))
  {
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open Image"), "",
        tr("Image File (*.png *.jpg *.bmp *.tga)"));
    if (fileName != nullptr)
    {
      bool success;
      QImage spec = il.loadImage(fileName, &success);
      if (!success)
        return;

      fs_watcher.addPath(fileName);
      spec = spec.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
      p->loadSpecularMap(fileName, spec);
    }
  }
  else if (option == tr("Reset specular map"))
  {
    bool succes;
    fs_watcher.removePath(p->get_specular_path());
    QImage specular = il.loadImage(p->get_name(), &succes);
    specular =
        specular.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    p->loadSpecularMap(p->get_name(), specular);
  }
  else if (option == tr("Add new frames"))
  {
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this, tr("Open Image"), "",
        tr("Image File (*.png *.jpg *.bmp *.tga)"));
    foreach (QString fileName, fileNames)
    {
      if (fileName != nullptr)
      {
        bool success;
        QImage image = il.loadImage(fileName, &success);
        if (!success)
          return;

        fs_watcher.addPath(fileName);
        image = image.convertToFormat(
            QImage::Format_RGBA8888_Premultiplied);
        p->loadImage(fileName, image);
      }
    }
  }
  else if (option == tr("Stop Animation"))
    p->animation.stop();
  else if (option == tr("Start Animation"))
    p->animation.start();
  else if (option == tr("Next Frame"))
    p->set_current_frame_id(p->get_current_frame_id() + 1);
  else if (option == tr("Previous Frame"))
    p->set_current_frame_id(p->get_current_frame_id() - 1);
  else if (option == tr("Remove current frame"))
  {
    p->remove_current_frame();
    fs_watcher.removePath(p->get_current_frame()->fileName);
  }
  else if (option == tr("Split in frames"))
  {
    int h_frames, v_frames;
    FrameSplitter fs(&h_frames, &v_frames);
    fs.exec();
    if (h_frames > 0 && v_frames > 0)
    {
      QImage original;
      p->get_current_frame()->get_image(TextureTypes::Diffuse, &original);
      ImageProcessor *n_p = new ImageProcessor;
      n_p->set_name(p->get_name()+"(frames)");
      QString filePath = p->get_current_frame()->get_file_name();
      for (int i=0; i<v_frames; i++)
      {
        for (int j=0; j<h_frames; j++)
        {
          QString frame_number = QString("%1").arg(j+i*h_frames, (int)(log10(v_frames*h_frames)+1), 10, QChar('0'));
          QString path = filePath.split(".").join("_"+frame_number+".");
          QPoint top_left(j*original.width()/h_frames,i*original.height()/v_frames);
          QSize size(original.width()/h_frames, original.height()/v_frames);
          n_p->loadImage(path,original.copy(QRect(top_left,size)));
        }
      }
      add_processor(n_p);
    }
  }
}

MainWindow::~MainWindow()
{
  QSettings settings("Azagaya", "Laigter");
  settings.setValue("geometry", saveGeometry());
  settings.setValue("windowState", saveState());
  delete ui;
}

void MainWindow::update_scene()
{
  ui->openGLPreviewWidget->need_to_update = true;
}

void MainWindow::on_actionOpen_triggered()
{
  QStringList fileNames = QFileDialog::getOpenFileNames(
      this, tr("Open Image"), "", tr("Image File (*.png *.jpg *.bmp *.tga)"));
  open_files(fileNames);
}

void MainWindow::add_processor(ImageProcessor *p)
{
  processorList.append(p);
  processor = p;
  on_comboBoxView_currentIndexChanged(ui->comboBoxView->currentIndex());
  QListWidgetItem *i = new QListWidgetItem(p->get_name());
  i->setData(Qt::UserRole, p->get_name());
  i->setIcon(QIcon(QPixmap::fromImage(*p->get_texture())));
  ui->listWidget->addItem(i);
  ui->listWidget->setCurrentRow(ui->listWidget->count() - 1);
}

ImageProcessor *MainWindow::find_processor(QString name)
{
  foreach (ImageProcessor *p, processorList)
  {
    if (p->get_name() == name)
      return p;
  }
  return nullptr;
}

void MainWindow::open_files(QStringList fileNames)
{
  QImage auximage;
  QStringList similarFiles;
  QStringList checkedFiles;
  foreach (QString fileName, fileNames)
  {
    if (similarFiles.contains(fileName))
      continue;

    /* Check for auto loading of frames */
    QStringList similarList;
    QString prefix, postfix;
    QFileInfo info(fileName);
    if (!checkedFiles.contains(fileName))
    {
      QRegularExpression rx("((\\d+)(?!.*\\d))");
      QRegularExpressionMatch match = rx.match(info.fileName());
      QStringList parts = fileName.split("/").last().split(match.captured(0));
      prefix = parts.first();
      postfix = parts.last();
      QDir dir = info.absoluteDir();
      if (prefix != "")
      {
        foreach (QString file, dir.entryList())
        {
          qDebug() << file;
          match = rx.match(file);

          QStringList parts = file.split("/").last().split(match.captured(0));
          qDebug() << prefix << parts.first() << postfix << parts.last();
          if (parts.first() == prefix && parts.last() == postfix)
          {
            similarList.append(dir.path() + "/" + file);
          }
        }
      }
    }

    QString name = info.baseName();
    if (similarList.count() == 0)
      similarList.append(fileName);

    if (similarList.count() > 1)
    {
      QMessageBox::StandardButton reply;
      reply = QMessageBox::question(
          this, "Load as Animation?",
          "Images with similar names where detected in the same folder. "
          "Load as Animation?",
          QMessageBox::Yes | QMessageBox::No);

      if (reply == QMessageBox::No)
      {
        checkedFiles = similarList;
        similarList.clear();
        similarList.append(fileName);
      }
      else
      {
        similarFiles = similarList;
        name = prefix;
      }
    }

    ImageProcessor *p = new ImageProcessor();
    int i = 1;
    while (ui->listWidget->findItems(name, Qt::MatchExactly).count())
      name = info.baseName() + " (" + QString::number(++i) + ")";

    p->set_name(name);
    p->copy_settings(processor->get_settings());

    bool loaded = false;
    foreach (fileName, similarList)
    {
      if (fileName != nullptr)
      {
        ImageLoader il;
        bool succes;
        auximage = il.loadImage(fileName, &succes);
        loaded = loaded || succes;
        if (!succes || auximage.isNull())
        {
          QMessageBox msgBox;
          msgBox.setText(tr("Cannot open ") + fileName + ".\n" +
                         tr("Unsupported or incorrect format."));
          msgBox.exec();
          continue;
        }
        auximage = auximage.convertToFormat(
            QImage::Format_RGBA8888_Premultiplied);
        p->loadImage(fileName, auximage);
        fs_watcher.addPath(fileName);
      }
    }
    if (loaded)
      add_processor(p);
    else
      delete p;
  }
}

void MainWindow::on_actionFitZoom_triggered()
{
  ui->openGLPreviewWidget->fitZoom();
}

void MainWindow::on_actionZoom_100_triggered()
{
  ui->openGLPreviewWidget->resetZoom();
}

void MainWindow::on_actionZoomIn_triggered()
{
  ui->openGLPreviewWidget->setZoom(1.1 * ui->openGLPreviewWidget->getZoom());
}

void MainWindow::on_actionZoomOut_triggered()
{
  ui->openGLPreviewWidget->setZoom(0.9 * ui->openGLPreviewWidget->getZoom());
}

void MainWindow::on_actionExport_triggered()
{
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Image"), "", tr("Image File (*.png *.jpg *.bmp)"));
  if (fileName == "")
    return;

  QString aux;
  QImage n;
  QString suffix;
  QFileInfo info;
  QString message = "";
  info = QFileInfo(fileName);
  suffix = info.completeSuffix();
  ui->listWidget->setCurrentItem(ui->listWidget->selectedItems().at(0));

  if (suffix == "")
    suffix = "png";

  if (ui->checkBoxExportNormal->isChecked())
  {
    aux = info.absoluteFilePath().remove("." + suffix) + "_n." + suffix;
    n = *processor->get_normal();
    n.save(aux);
    message += tr("Normal map was exported.\n");
  }

  if (ui->checkBoxExportParallax->isChecked())
  {
    aux = info.absoluteFilePath().remove("." + suffix) + "_p." + suffix;
    n = *processor->get_parallax();
    n.save(aux);
    message += tr("Parallax map was exported.\n");
  }

  if (ui->checkBoxExportSpecular->isChecked())
  {
    aux = info.absoluteFilePath().remove("." + suffix) + "_s." + suffix;
    n = *processor->get_specular();
    n.save(aux);
    message += tr("Specular map was exported.\n");
  }

  if (ui->checkBoxExportOcclusion->isChecked())
  {
    fileName =
        info.absoluteFilePath().remove("." + suffix) + "_o." + suffix;
    n = *processor->get_occlusion();
    n.save(fileName);
    message += tr("Occlussion map was exported.\n");
  }

  if (ui->checkBoxExportPreview->isChecked())
  {
    n = ui->openGLPreviewWidget->get_preview(false);
    fileName =
        info.absoluteFilePath().remove("." + suffix) + "_v." + suffix;
    n.save(fileName);
    message += tr("Preview was exported.\n");
  }

  if (message != "")
  {
    QMessageBox msgBox;
    msgBox.setText(message);
    msgBox.exec();
  }
}

void MainWindow::openGL_initialized()
{
  QString tmpImage = ":/images/sample.png";
  bool success;
  processor_selected(processor, false);
  processor->loadImage(tmpImage, il.loadImage(tmpImage, &success));
  ui->openGLPreviewWidget->add_processor(processor);
  ui->openGLPreviewWidget->loadTextures();
  update_scene();
  processor_selected(processor, true);
  processor->set_light_list(
      *(ui->openGLPreviewWidget->get_current_light_list_ptr()));
  on_comboBoxView_currentIndexChanged(Texture);
  on_actionLoadPlugins_triggered();
}

void MainWindow::on_pushButtonColor_clicked()
{
  QColorDialog *cd = new QColorDialog(currentColor);
  connect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
          SLOT(set_light_color(const QColor &)));
  cd->exec();
  disconnect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
             SLOT(set_light_color(const QColor &)));
  delete cd;
}

void MainWindow::set_light_color(const QColor &color)
{
  if (color.isValid())
  {
    currentColor = color;
    QPixmap pixmap(100, 100);
    pixmap.fill(color);
    ui->pushButtonColor->setIcon(QIcon(pixmap));
    ui->openGLPreviewWidget->setLightColor(color);
    ui->openGLPreviewWidget->setSpecColor(color);
  }
}

void MainWindow::on_horizontalSliderDiffHeight_valueChanged(int value)
{
  ui->openGLPreviewWidget->setLightHeight(value / 100.0);
}

void MainWindow::on_horizontalSliderDiffLight_valueChanged(int value)
{
  ui->openGLPreviewWidget->setLightIntensity(value / 100.0);
}

void MainWindow::on_horizontalSliderAmbientLight_valueChanged(int value)
{
  ui->openGLPreviewWidget->setAmbientIntensity(value / 100.0);
}

void MainWindow::on_pushButtonAmbientColor_clicked()
{
  QColorDialog *cd = new QColorDialog(currentColor);
  connect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
          SLOT(set_ambient_color(const QColor &)));
  cd->exec();
  disconnect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
             SLOT(set_ambient_color(const QColor &)));
  delete cd;
}

void MainWindow::set_ambient_color(const QColor &color)
{
  if (color.isValid())
  {
    currentAmbientcolor = color;
    QPixmap pixmap(100, 100);
    pixmap.fill(color);
    ui->pushButtonAmbientColor->setIcon(QIcon(pixmap));
    ui->openGLPreviewWidget->setAmbientColor(color);
  }
}

void MainWindow::connect_processor(ImageProcessor *p)
{
  connect(p, SIGNAL(processed()), this, SLOT(update_scene()));
  connect(ui->normalDepthSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_normal_depth(int)));
  connect(ui->normalBlurSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_normal_blur_radius(int)));
  connect(ui->normalBevelSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_normal_bisel_depth(int)));
  connect(ui->normalBiselDistanceSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_normal_bisel_distance(int)));
  connect(ui->normalBiselBlurSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_normal_bisel_blur_radius(int)));
  connect(ui->biselSoftRadio, SIGNAL(toggled(bool)), p,
          SLOT(set_normal_bisel_soft(bool)));
  connect(ui->normalInvertX, SIGNAL(toggled(bool)), p,
          SLOT(set_normal_invert_x(bool)));
  connect(ui->normalInvertY, SIGNAL(toggled(bool)), p,
          SLOT(set_normal_invert_y(bool)));
  connect(ui->checkBoxTileable, SIGNAL(toggled(bool)), p,
          SLOT(set_tileable(bool)));
  connect(ui->checkBoxParallaxInvert, SIGNAL(toggled(bool)), p,
          SLOT(set_parallax_invert(bool)));
  connect(ui->parallaxSoftSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_soft(int)));
  connect(ui->parallaxThreshSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_thresh(int)));
  connect(ui->parallaxFocusSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_focus(int)));
  connect(ui->parallaxMinHeight, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_min(int)));
  connect(ui->parallaxQuantizationSlider, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_quantization(int)));
  connect(ui->sliderParallaxErodeDilate, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_erode_dilate(int)));
  connect(ui->sliderParallaxBright, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_brightness(int)));
  connect(ui->sliderParallaxContrast, SIGNAL(valueChanged(int)), p,
          SLOT(set_parallax_contrast(int)));
  connect(ui->sliderSpecSoft, SIGNAL(valueChanged(int)), p,
          SLOT(set_specular_blur(int)));
  connect(ui->sliderSpecBright, SIGNAL(valueChanged(int)), p,
          SLOT(set_specular_bright(int)));
  connect(ui->sliderSpecContrast, SIGNAL(valueChanged(int)), p,
          SLOT(set_specular_contrast(int)));
  connect(ui->sliderSpecThresh, SIGNAL(valueChanged(int)), p,
          SLOT(set_specular_thresh(int)));
  connect(ui->checkBoxSpecInvert, SIGNAL(toggled(bool)), p,
          SLOT(set_specular_invert(bool)));
  connect(ui->sliderOcclusionSoft, SIGNAL(valueChanged(int)), p,
          SLOT(set_occlusion_blur(int)));
  connect(ui->sliderOcclusionBright, SIGNAL(valueChanged(int)), p,
          SLOT(set_occlusion_bright(int)));
  connect(ui->sliderOcclusionContrast, SIGNAL(valueChanged(int)), p,
          SLOT(set_occlusion_contrast(int)));
  connect(ui->sliderOcclusionThresh, SIGNAL(valueChanged(int)), p,
          SLOT(set_occlusion_thresh(int)));
  connect(ui->checkBoxOcclusionInvert, SIGNAL(toggled(bool)), p,
          SLOT(set_occlusion_invert(bool)));
  connect(ui->checkBoxOcclusionDistance, SIGNAL(toggled(bool)), p,
          SLOT(set_occlusion_distance_mode(bool)));
  connect(ui->sliderOcclusionDistance, SIGNAL(valueChanged(int)), p,
          SLOT(set_occlusion_distance(int)));
  connect(ui->checkBoxMosaicoX, SIGNAL(toggled(bool)), p,
          SLOT(set_tile_x(bool)));
  connect(ui->checkBoxMosaicoY, SIGNAL(toggled(bool)), p,
          SLOT(set_tile_y(bool)));
  connect(ui->checkBoxParallax, SIGNAL(toggled(bool)), p,
          SLOT(set_is_parallax(bool)));
  p->set_connected(true);
}

void MainWindow::disconnect_processor(ImageProcessor *p)
{
  if (!p->animation.isActive() || !ui->openGLPreviewWidget->get_processor_list()->contains(p))
  {
    disconnect(p, SIGNAL(processed()), this, SLOT(update_scene()));
  }
  disconnect(ui->normalDepthSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_normal_depth(int)));
  disconnect(ui->normalBlurSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_normal_blur_radius(int)));
  disconnect(ui->normalBevelSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_normal_bisel_depth(int)));
  disconnect(ui->normalBiselDistanceSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_normal_bisel_distance(int)));
  disconnect(ui->normalBiselBlurSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_normal_bisel_blur_radius(int)));
  disconnect(ui->biselSoftRadio, SIGNAL(toggled(bool)), p,
             SLOT(set_normal_bisel_soft(bool)));
  disconnect(ui->normalInvertX, SIGNAL(toggled(bool)), p,
             SLOT(set_normal_invert_x(bool)));
  disconnect(ui->normalInvertY, SIGNAL(toggled(bool)), p,
             SLOT(set_normal_invert_y(bool)));
  disconnect(ui->openGLPreviewWidget, SIGNAL(initialized()), this,
             SLOT(openGL_initialized()));
  disconnect(ui->checkBoxTileable, SIGNAL(toggled(bool)), p,
             SLOT(set_tileable(bool)));
  disconnect(ui->checkBoxParallaxInvert, SIGNAL(toggled(bool)), p,
             SLOT(set_parallax_invert(bool)));
  disconnect(ui->parallaxSoftSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_soft(int)));
  disconnect(ui->parallaxThreshSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_thresh(int)));
  disconnect(ui->parallaxFocusSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_focus(int)));
  disconnect(ui->parallaxMinHeight, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_min(int)));
  disconnect(ui->parallaxQuantizationSlider, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_quantization(int)));
  disconnect(ui->sliderParallaxErodeDilate, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_erode_dilate(int)));
  disconnect(ui->sliderParallaxBright, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_brightness(int)));
  disconnect(ui->sliderParallaxContrast, SIGNAL(valueChanged(int)), p,
             SLOT(set_parallax_contrast(int)));
  disconnect(ui->sliderSpecSoft, SIGNAL(valueChanged(int)), p,
             SLOT(set_specular_blur(int)));
  disconnect(ui->sliderSpecBright, SIGNAL(valueChanged(int)), p,
             SLOT(set_specular_bright(int)));
  disconnect(ui->sliderSpecContrast, SIGNAL(valueChanged(int)), p,
             SLOT(set_specular_contrast(int)));
  disconnect(ui->sliderSpecThresh, SIGNAL(valueChanged(int)), p,
             SLOT(set_specular_thresh(int)));
  disconnect(ui->checkBoxSpecInvert, SIGNAL(toggled(bool)), p,
             SLOT(set_specular_invert(bool)));
  disconnect(ui->sliderOcclusionSoft, SIGNAL(valueChanged(int)), p,
             SLOT(set_occlusion_blur(int)));
  disconnect(ui->sliderOcclusionBright, SIGNAL(valueChanged(int)), p,
             SLOT(set_occlusion_bright(int)));
  disconnect(ui->sliderOcclusionContrast, SIGNAL(valueChanged(int)), p,
             SLOT(set_occlusion_contrast(int)));
  disconnect(ui->sliderOcclusionThresh, SIGNAL(valueChanged(int)), p,
             SLOT(set_occlusion_thresh(int)));
  disconnect(ui->checkBoxOcclusionInvert, SIGNAL(toggled(bool)), p,
             SLOT(set_occlusion_invert(bool)));
  disconnect(ui->checkBoxOcclusionDistance, SIGNAL(toggled(bool)), p,
             SLOT(set_occlusion_distance_mode(bool)));
  disconnect(ui->sliderOcclusionDistance, SIGNAL(valueChanged(int)), p,
             SLOT(set_occlusion_distance(int)));
  disconnect(ui->checkBoxMosaicoX, SIGNAL(toggled(bool)), p,
             SLOT(set_tile_x(bool)));
  disconnect(ui->checkBoxMosaicoY, SIGNAL(toggled(bool)), p,
             SLOT(set_tile_y(bool)));
  disconnect(ui->checkBoxParallax, SIGNAL(toggled(bool)), p,
             SLOT(set_is_parallax(bool)));
  p->set_connected(false);
}

void MainWindow::on_listWidget_itemSelectionChanged()
{
  ImageProcessor *p = nullptr;
  if (ui->listWidget->count() == 0)
    processor_selected(sample_processor, true);
  else if (ui->listWidget->selectedItems().count() > 0)
  {
    processor_selected(sample_processor, false);
    ui->openGLPreviewWidget->clear_processor_list();
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
      foreach (QListWidgetItem *item, ui->listWidget->selectedItems())
      {
        if (ui->listWidget->item(i)->data(Qt::UserRole).toString() ==
            item->data(Qt::UserRole).toString())
        {
          p = find_processor(item->data(Qt::UserRole).toString());
          if (p)
          {
            ui->openGLPreviewWidget->add_processor(p);
            processor_selected(p, true);
            break;
          }
        }
      }
    }
  }
  else
    ui->listWidget->setCurrentRow(0);

  ui->openGLPreviewWidget->need_to_update = true;
}


void MainWindow::ExportMap(TextureTypes type, ImageProcessor *p, QString postfix){
  QImage n;
  QString suffix;
  QString name;
  QFileInfo info;
  for (int i=0; i< p->frames.count(); i++)
  {
    p->frames[i].get_image(type, &n);
    QString file_name = p->frames[i].get_file_name();
    if (!file_name.startsWith("/"))
    {
      info = QFileInfo(project.GetCurrentPath());
      file_name = info.dir().path() + "/" + file_name.split("/").last();
    }
    info = QFileInfo(file_name);
    suffix = info.completeSuffix();
    name = info.absoluteFilePath().remove("." + suffix) + postfix + "." + suffix;
    n.save(name);

  }
}

void MainWindow::on_pushButton_clicked()
{

  QString message = "";

    foreach (ImageProcessor *p, processorList)
    {
      if (ui->checkBoxExportNormal->isChecked())
      {
        ExportMap(TextureTypes::Normal, p, "_n");
      }
      if (ui->checkBoxExportParallax->isChecked())
      {
        ExportMap(TextureTypes::Parallax, p, "_p");
      }
      if (ui->checkBoxExportSpecular->isChecked())
      {
        ExportMap(TextureTypes::Specular, p, "_s");
      }
      if (ui->checkBoxExportOcclusion->isChecked())
      {
        ExportMap(TextureTypes::Occlussion, p, "_o");
      }
    }
  if (ui->checkBoxExportPreview->isChecked())
  {
    QImage n;
    QString suffix;
    QString name;
    QFileInfo info;
    foreach (ImageProcessor *p, processorList)
    {
      for (int i=0; i< p->frames.count(); i++)
      {
        n = ui->openGLPreviewWidget->get_preview(false, false);
        info = QFileInfo(p->frames[i].get_file_name());
        suffix = info.completeSuffix();
        name = info.absoluteFilePath().remove("." + suffix) + "_v." + suffix;
        n.save(name);
      }
    }
  }

  message = tr("All selected maps were exported.\n");
  QMessageBox msgBox;
  msgBox.setText(message);
  msgBox.exec();
}

void MainWindow::on_pushButtonBackgroundColor_clicked()
{
  QColorDialog *cd = new QColorDialog(currentColor);
  connect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
          SLOT(set_background_color(const QColor &)));
  cd->exec();
  disconnect(cd, SIGNAL(currentColorChanged(const QColor &)), this,
             SLOT(set_background_color(const QColor &)));
  delete cd;
}

void MainWindow::set_background_color(const QColor &color)
{
  if (color.isValid())
  {
    currentBackgroundColor = color;
    QPixmap pixmap(100, 100);
    pixmap.fill(color);
    ui->pushButtonBackgroundColor->setIcon(QIcon(pixmap));
    ui->openGLPreviewWidget->setBackgroundColor(color);
  }
}

void MainWindow::on_pushButton_2_clicked()
{
  NBSelector *nb = new NBSelector(processor);
  nb->exec();
}

void MainWindow::on_comboBox_currentIndexChanged(int index)
{
  ParallaxType ptype = static_cast<ParallaxType>(index);
  if (ui->listWidget->count() > 0)
  {
    foreach (ImageProcessor *processor, processorList)
    {
      if (processor->get_selected() && processor->get_connected())
        processor->set_parallax_type(ptype);
    }
  }
  else
    sample_processor->set_parallax_type(ptype);

  switch (ptype)
  {
    case ParallaxType::Binary:
    {
      ui->parallaxMinHeight->setVisible(true);
      ui->parallaxSoftSlider->setVisible(true);
      ui->parallaxFocusSlider->setVisible(true);
      ui->parallaxThreshSlider->setVisible(true);
      ui->checkBoxParallaxInvert->setVisible(true);
      ui->parallaxQuantizationSlider->setVisible(false);
      ui->sliderParallaxErodeDilate->setVisible(true);
      ui->sliderParallaxBright->setVisible(false);
      ui->sliderParallaxContrast->setVisible(false);
      ui->labelErodeDilate->setVisible(true);
      ui->labelBrightness->setVisible(false);
      ui->labelContrast->setVisible(false);
      ui->labelThreshMin->setVisible(true);
      ui->labelThreshSoft->setVisible(true);
      ui->labelThreshFocus->setVisible(true);
      ui->labelThreshParallax->setVisible(true);
      ui->labelQuantization->setVisible(false);
      break;
    }

    case ParallaxType::HeightMap:
    {
      ui->parallaxMinHeight->setVisible(false);
      ui->parallaxSoftSlider->setVisible(true);
      ui->parallaxFocusSlider->setVisible(false);
      ui->parallaxThreshSlider->setVisible(false);
      ui->checkBoxParallaxInvert->setVisible(true);
      ui->parallaxQuantizationSlider->setVisible(false);
      ui->sliderParallaxErodeDilate->setVisible(false);
      ui->sliderParallaxBright->setVisible(true);
      ui->sliderParallaxContrast->setVisible(true);
      ui->labelErodeDilate->setVisible(false);
      ui->labelBrightness->setVisible(true);
      ui->labelContrast->setVisible(true);
      ui->labelThreshMin->setVisible(false);
      ui->labelThreshSoft->setVisible(true);
      ui->labelThreshFocus->setVisible(false);
      ui->labelThreshParallax->setVisible(false);
      ui->labelQuantization->setVisible(false);
      break;
    }

    case ParallaxType::Quantization:
    {
      ui->parallaxMinHeight->setVisible(true);
      ui->parallaxSoftSlider->setVisible(true);
      ui->parallaxFocusSlider->setVisible(true);
      ui->parallaxThreshSlider->setVisible(true);
      ui->checkBoxParallaxInvert->setVisible(true);
      ui->parallaxQuantizationSlider->setVisible(true);
      ui->sliderParallaxErodeDilate->setVisible(false);
      ui->sliderParallaxBright->setVisible(true);
      ui->sliderParallaxContrast->setVisible(true);
      ui->labelErodeDilate->setVisible(false);
      ui->labelBrightness->setVisible(true);
      ui->labelContrast->setVisible(true);
      ui->labelThreshMin->setVisible(true);
      ui->labelThreshSoft->setVisible(true);
      ui->labelThreshFocus->setVisible(true);
      ui->labelThreshParallax->setVisible(true);
      ui->labelQuantization->setVisible(true);
    }
  }
}

void MainWindow::on_pushButtonExportTo_clicked()
{
  QImage n;
  QString suffix;
  QString name;
  QFileInfo info;
  QString message = "";
  QString path = QFileDialog::getExistingDirectory();

  if (path != nullptr)
  {
    if (ui->checkBoxExportNormal->isChecked())
    {
      foreach (ImageProcessor *p, processorList)
      {
        n = *p->get_normal();
        info = QFileInfo(p->get_name());
        suffix = info.completeSuffix();
        name = path + "/" + info.baseName() + "_n." + suffix;
        int i = 1;
        while (QFileInfo::exists(name))
          name = path + "/" + info.baseName() + "(" +
                 QString::number(++i) + ")" + "_n." + suffix;

        n.save(name);
      }
      message += tr("All normal maps were exported.\n");
    }

    if (ui->checkBoxExportParallax->isChecked())
    {
      foreach (ImageProcessor *p, processorList)
      {
        n = *p->get_parallax();
        info = QFileInfo(p->get_name());
        suffix = info.completeSuffix();
        name = path + "/" + info.baseName() + "_p." + suffix;
        int i = 1;
        while (QFileInfo::exists(name))
          name = path + "/" + info.baseName() + "(" +
                 QString::number(++i) + ")" + "_p." + suffix;

        n.save(name);
      }
      message += tr("All parallax maps were exported.\n");
    }

    if (ui->checkBoxExportSpecular->isChecked())
    {
      foreach (ImageProcessor *p, processorList)
      {
        n = *p->get_specular();
        info = QFileInfo(p->get_name());
        suffix = info.completeSuffix();
        name = path + "/" + info.baseName() + "_s." + suffix;
        int i = 1;
        while (QFileInfo::exists(name))
          name = path + "/" + info.baseName() + "(" +
                 QString::number(++i) + ")" + "_s." + suffix;

        n.save(name);
      }
      message += tr("All specular maps were exported.\n");
    }

    if (ui->checkBoxExportOcclusion->isChecked())
    {
      foreach (ImageProcessor *p, processorList)
      {
        n = *p->get_occlusion();
        info = QFileInfo(p->get_name());
        suffix = info.completeSuffix();
        name = path + "/" + info.baseName() + "_o." + suffix;
        int i = 1;
        while (QFileInfo::exists(name))
          name = path + "/" + info.baseName() + "(" +
                 QString::number(++i) + ")" + "_o." + suffix;

        n.save(name);
      }
      message += tr("All occlussion maps were exported.\n");
    }

    if (ui->checkBoxExportPreview->isChecked())
    {
      ui->openGLPreviewWidget->get_preview(false, true, path);
      message += tr("All previews were exported.\n");
    }

    if (message != "")
    {
      QMessageBox msgBox;
      msgBox.setText(message);
      msgBox.exec();
    }
  }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
  if (e->mimeData()->hasUrls())
    e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
  QStringList fileNames;
  QStringList projectNames;
  QList<QUrl> urlList = event->mimeData()->urls();
  openDroppedFiles(urlList, &fileNames);
  foreach (QString path, fileNames)
  {
    if (path.endsWith(".laigter"))
    {
      projectNames.append(path);
      fileNames.removeOne(path);
    }
  }
  if (projectNames.count() > 0)
  {
    if (projectNames.count() > 1)
    {
      QMessageBox msg_box;
      msg_box.setText("Only one project at a time can be loaded for now.");
      msg_box.exec();
    }
    LoadProject(projectNames[0]);
  }
  open_files(fileNames);
}

void MainWindow::openDroppedFiles(QList<QUrl> urlList, QStringList *fileNames)
{
  foreach (QUrl url, urlList)
  {
    QFileInfo info(url.toLocalFile());
    if (info.isFile())
      fileNames->append(url.toLocalFile());
    else if (info.isDir())
    {
      QList<QUrl> uList;
      QDir dir(url.toLocalFile());
      foreach (QFileInfo entry, dir.entryInfoList())
        uList.append(QUrl::fromLocalFile(entry.absoluteFilePath()));

      /* Always remove 0 and 1 cause they are current dir and parent dir
             */
      uList.removeAt(0);
      uList.removeAt(0);
      openDroppedFiles(uList, fileNames);
    }
  }
}

void MainWindow::on_actionPresets_triggered()
{
  PresetsManager pm(processor->get_settings(), &processorList);
  connect(&pm, SIGNAL(settingAplied()), this,
          SLOT(on_listWidget_itemSelectionChanged()));
  pm.exec();
  disconnect(&pm, SIGNAL(settingAplied()), this,
             SLOT(on_listWidget_itemSelectionChanged()));
}

void MainWindow::on_horizontalSliderSpec_valueChanged(int value)
{
  ui->openGLPreviewWidget->setSpecIntensity(
      static_cast<float>(value / 100.0));
}

void MainWindow::on_horizontalSliderSpecScatter_valueChanged(int value)
{
  ui->openGLPreviewWidget->setSpecScatter(value);
}

void MainWindow::on_comboBoxView_currentIndexChanged(int index)
{
  ui->openGLPreviewWidget->setLight(false);
  if (index == ViewMode::Preview)
    ui->actionAdd_Light->setEnabled(true);
  else
  {
    ui->actionAdd_Light->setEnabled(false);
    ui->openGLPreviewWidget->set_add_light(false);
    ui->actionAdd_Light->setChecked(false);
  }

  switch (index)
  {
    case ViewMode::Texture:
    {
      ui->openGLPreviewWidget->set_view_mode(Texture);
      break;
    }

    case ViewMode::NormalMap:
    {
      ui->openGLPreviewWidget->set_view_mode(NormalMap);
      break;
    }

    case ViewMode::SpecularMap:
    {
      ui->openGLPreviewWidget->set_view_mode(SpecularMap);
      break;
    }

    case ViewMode::ParallaxMap:
    {
      ui->openGLPreviewWidget->set_view_mode(ParallaxMap);
      break;
    }

    case ViewMode::OcclusionMap:
    {
      ui->openGLPreviewWidget->set_view_mode(OcclusionMap);
      break;
    }

    case ViewMode::Preview:
    {
      ui->openGLPreviewWidget->setLight(true);
      ui->openGLPreviewWidget->set_view_mode(Preview);
      break;
    }
  }
}

void MainWindow::on_actionExportPreview_triggered()
{
  int current_view = ui->comboBoxView->currentIndex();
  ui->comboBoxView->setCurrentIndex(ViewMode::Preview);
  QImage preview = ui->openGLPreviewWidget->get_preview();
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Image"), "", tr("Image File (*.png *.jpg *.bmp)"));

  if (fileName == "")
    return;

  QMessageBox msgBox;
  if (preview.save(fileName))
    msgBox.setText("Preview was exported.");
  else
    msgBox.setText("Could not export preview.");

  msgBox.exec();
  ui->comboBoxView->setCurrentIndex(current_view);
}

void MainWindow::on_actionAbout_triggered()
{
  AboutDialog *ad = new AboutDialog;
  ad->show();
}

void MainWindow::on_actionAdd_Light_triggered(bool checked)
{
  ui->openGLPreviewWidget->set_add_light(checked);
}

void MainWindow::selectedLightChanged(LightSource *light)
{
  ui->horizontalSliderDiffLight->setValue(light->get_diffuse_intensity() *
                                          100);
  ui->horizontalSliderSpec->setValue(light->get_specular_intesity() * 100);
  ui->horizontalSliderSpecScatter->setValue(light->get_specular_scatter());
  ui->horizontalSliderDiffHeight->setValue(light->get_height() * 100);

  QPixmap pixmap(100, 100);
  currentColor = light->get_diffuse_color();
  pixmap.fill(currentColor);
  ui->pushButtonColor->setIcon(QIcon(pixmap));
}

void MainWindow::stopAddingLight()
{
  ui->actionAdd_Light->setChecked(false);
  on_actionAdd_Light_triggered(false);
}

void MainWindow::on_checkBoxLightsPerTexture_toggled(bool checked)
{
  ui->openGLPreviewWidget->use_sample_light_list(!checked);
}

void MainWindow::selectedProcessorsChanged(QList<ImageProcessor *> list)
{
  selectedProcessors = list;
  foreach (ImageProcessor *p, processorList)
    disconnect_processor(p);

  foreach (ImageProcessor *p, selectedProcessors)
    connect_processor(p);
}

void MainWindow::processor_selected(ImageProcessor *processor, bool selected)
{
  foreach (ImageProcessor *p, processorList)
    disconnect_processor(p);

  processor->set_selected(selected);
  set_enabled_map_controls(false);
  if (selected)
  {
    ui->openGLPreviewWidget->processor = processor;
    ui->normalInvertX->setChecked(processor->get_normal_invert_x() == -1);
    ui->normalInvertY->setChecked(processor->get_normal_invert_y() == -1);
    ui->biselSoftRadio->setChecked(processor->get_normal_bisel_soft());
    ui->biselAbruptRadio->setChecked(!processor->get_normal_bisel_soft());
    ui->normalBlurSlider->setValue(processor->get_normal_blur_radius());
    ui->normalBevelSlider->setValue(processor->get_normal_bisel_depth());
    ui->normalDepthSlider->setValue(processor->get_normal_depth());
    ui->normalBiselBlurSlider->setValue(
        processor->get_normal_bisel_blur_radius());
    ui->normalBiselDistanceSlider->setValue(
        processor->get_normal_bisel_distance());
    ui->checkBoxTileable->setChecked(processor->get_tileable());
    ui->parallaxSoftSlider->setValue(processor->get_parallax_soft());
    ui->parallaxFocusSlider->setValue(processor->get_parallax_focus());
    ui->parallaxThreshSlider->setValue(processor->get_parallax_thresh());
    ui->checkBoxParallaxInvert->setChecked(
        processor->get_parallax_invert());
    ui->comboBox->setCurrentIndex(
        static_cast<int>(processor->get_parallax_type()));
    ui->parallaxMinHeight->setValue(processor->get_parallax_min());
    ui->parallaxQuantizationSlider->setValue(
        processor->get_parallax_quantization());
    ui->sliderParallaxBright->setValue(
        processor->get_parallax_brightness());
    ui->sliderParallaxContrast->setValue(
        static_cast<int>(processor->get_parallax_contrast() * 1000));
    ui->sliderParallaxErodeDilate->setValue(
        processor->get_parallax_erode_dilate());
    ui->sliderSpecSoft->setValue(processor->get_specular_blur());
    ui->sliderSpecBright->setValue(processor->get_specular_bright());
    ui->sliderSpecThresh->setValue(processor->get_specular_trhesh());
    ui->sliderSpecContrast->setValue(
        static_cast<int>(processor->get_specular_contrast() * 1000));
    ui->checkBoxSpecInvert->setChecked(processor->get_specular_invert());
    ui->sliderOcclusionSoft->setValue(processor->get_occlusion_blur());
    ui->sliderOcclusionBright->setValue(processor->get_occlusion_bright());
    ui->sliderOcclusionThresh->setValue(processor->get_occlusion_trhesh());
    ui->sliderOcclusionContrast->setValue(
        static_cast<int>(processor->get_occlusion_contrast() * 1000));
    ui->sliderOcclusionDistance->setValue(
        processor->get_occlusion_distance());
    ui->checkBoxOcclusionInvert->setChecked(
        processor->get_occlusion_invert());
    ui->checkBoxOcclusionDistance->setChecked(
        processor->get_occlusion_distance_mode());
    this->processor = processor;
    ui->checkBoxMosaicoX->setChecked(processor->get_tile_x());
    ui->checkBoxMosaicoY->setChecked(processor->get_tile_y());
  }

  if (ui->listWidget->selectedItems().count() == 1)
  {
    foreach (ImageProcessor *p, processorList)
    {
      if (p->get_name() == ui->listWidget->selectedItems()
                               .at(0)
                               ->data(Qt::UserRole)
                               .toString())
        p->set_selected(true);
    }
  }

  foreach (ImageProcessor *p, processorList)
  {
    if (p->get_selected())
    {
      connect_processor(p);
      if (p->get_light_list_ptr()->count() > 0)
        ui->openGLPreviewWidget->set_current_light_list(
            p->get_light_list_ptr());
      set_enabled_map_controls(true);
    }
  }

  if (sample_processor->get_selected())
    connect_processor(sample_processor);

  if (ui->listWidget->count() == 0)
    set_enabled_map_controls(true);
}

void MainWindow::set_enabled_map_controls(bool e)
{
  ui->normalDockWidget->setEnabled(e);
  ui->specularDockWidget->setEnabled(e);
  ui->parallaxDockWidget->setEnabled(e);
  ui->occlusionDockWidget->setEnabled(e);
  ui->TileDockWidget->setEnabled(e);
  ui->checkBoxMosaicoX->setEnabled(e);
  ui->checkBoxMosaicoY->setEnabled(e);
  ui->checkBoxParallax->setEnabled(e);
}

void MainWindow::set_enabled_light_controls(bool e)
{
  ui->tabWidgetLight->setEnabled(e);
  ui->pushButtonColor->setEnabled(e);
  ui->horizontalSliderDiffHeight->setEnabled(e);
}

void MainWindow::onFileChanged(const QString &file_path)
{
  if (!QFile(file_path).exists())
    return;

  // Not needed anymore ???
  //  if (!fs_watcher.files().contains(file_path)) {
  //    fs_watcher.addPath(file_path);
  //  }
  // no file system watcher reports "change" when creating file (bug?)

  if (QFile(file_path).size() == 0)
    return;

  Q_FOREACH (ImageProcessor *ip, processorList)
  {
    QImage auximage;
    ImageLoader il;
    bool success;
    // IMPORTANT TODO, replace this with a function in ImageProcessor that takes a string and an image replaces all images with that path //
    auximage = il.loadImage(file_path, &success);
    if (file_path == ip->get_name())
      ip->loadImage(file_path, auximage);

    if (file_path == ip->get_specular_path())
      ip->loadSpecularMap(file_path, auximage);

    if (file_path == ip->get_heightmap_path())
      ip->loadHeightMap(file_path, auximage);
  }
  ui->openGLPreviewWidget->need_to_update = true;
}

void MainWindow::on_actionLoadPlugins_triggered()
{
  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir dir(appData);
  QDir tmp(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  dir.cd("plugins");
  const QStringList entryList = dir.entryList(QDir::Files);

  foreach (QDockWidget *dock, plugin_docks_list)
  {
    plugin_docks_list.removeOne(dock);
    delete dock;
  }

  foreach (QAction *action, ui->pluginToolBar->actions())
  {
    if (action->text() == "Load Plugins" || action->text() == "Install Plugin" ||action->text() == "Delete Plugin")
      continue;
    ui->pluginToolBar->removeAction(action);
  }

  for (const QString &fileName : entryList)
  {
    if (QFile(tmp.absoluteFilePath(fileName)).exists())
    {
      QFile(tmp.absoluteFilePath(fileName)).remove();
    }
      QFile(dir.absoluteFilePath(fileName)).copy(tmp.absoluteFilePath(fileName));
      QPluginLoader *pl =
          new QPluginLoader(tmp.absoluteFilePath(fileName));
      if (pl->metaData().value("MetaData").toObject().value("version").toDouble() < 1.9)
      {
        qDebug() << "incorrect plugin version.";
        pl->unload();
        delete pl;
        QFile plugin(dir.absoluteFilePath(fileName));
        plugin.remove();
        return;
      }

      BrushInterface *b = qobject_cast<BrushInterface *>(pl->instance());
      qDebug() << pl->errorString();
      if (b != nullptr)
      {
        ui->openGLPreviewWidget->currentBrush = b;
        b->setProcessor(&processor);
        QAction *action = new QAction(b->getIcon(), b->getName());
        action->setCheckable(true);
        QDockWidget *pluginDock = new QDockWidget(b->getName(), this);
        QWidget *pluginGui = b->loadGUI();

        addDockWidget(Qt::LeftDockWidgetArea, pluginDock);
        pluginDock->setFloating(true);
        pluginDock->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable);
        pluginDock->setWidget(pluginGui);
        pluginDock->setVisible(false);
        connect(action, SIGNAL(toggled(bool)), pluginDock,
                SLOT(setVisible(bool)));
        connect(b->getObject(),
                SIGNAL(selected_changed(BrushInterface *)), this,
                SLOT(select_plugin(BrushInterface *)));
        ui->pluginToolBar->addAction(action);
        b->set_selected(false);
        plugin_docks_list.append(pluginDock);
        plugin_list.append(pl);
        brush_list.append(b);
      }
    }

}

void MainWindow::select_plugin(BrushInterface *b)
{
  ui->openGLPreviewWidget->currentBrush = b;
  foreach (BrushInterface *pl, brush_list)
  {
    if (pl->getName() == b->getName())
      continue;
    else
      pl->set_selected(false);
  }
  ui->openGLPreviewWidget->need_to_update = true;
}

void MainWindow::on_actionInstall_Plugin_triggered()
{
#ifdef Q_OS_LINUX
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Plugin"), "", tr("Shared Library (*.so)"));
#elif defined(Q_OS_WIN)
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Plugin"), "", tr("Shared Library (*.dll)"));
#elif defined(Q_OS_MAC)
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Plugin"), "", tr("Shared Library (*.dylib)"));
#endif

  if (fileName != nullptr)
  {
    QFile f(fileName);
    QFileInfo i(f);
    QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appData + "/plugins/");
    QString new_plugin_path = dir.absoluteFilePath(i.fileName());
    if (QFile::exists(new_plugin_path))
      QFile::remove(new_plugin_path);

    f.copy(new_plugin_path);
    on_actionLoadPlugins_triggered();
  }
}

void MainWindow::on_actionDelete_Plugin_triggered()
{
  QString extension = "";
#ifdef Q_OS_LINUX
  extension = ".so";
#elif defined(Q_OS_WIN)
  extension = ".dll";
#elif defined(Q_OS_MAC)
  extension = ".dylib";
#endif

  if (ui->openGLPreviewWidget->currentBrush)
  {
    ui->openGLPreviewWidget->currentBrush->set_selected(false);
    ui->openGLPreviewWidget->currentBrush = nullptr;
  }

  foreach (QPluginLoader *pl, plugin_list)
  {
    plugin_list.removeOne(pl);
    pl->unload();
    delete pl;
  }

  foreach (BrushInterface *pl, brush_list)
    brush_list.removeOne(pl);

  QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir dir(appData);
  dir.cd("plugins");
  QStringList entryList = dir.entryList(QDir::Files);
  for (const QString &fileName : entryList)
  {
    if (!fileName.endsWith(extension))
      entryList.removeOne(fileName);
  }

  RemovePluginDialog rd;
  rd.setPluginList(entryList);
  rd.exec();
  on_actionLoadPlugins_triggered();
}

void MainWindow::on_actionLanguages_triggered() { el->show(); }

void MainWindow::changeEvent(QEvent *event)
{
  // QDialog::changeEvent(event);
  if (QEvent::LanguageChange == event->type())
    retranslate();
}

void MainWindow::retranslate()
{
  ui->retranslateUi(this);
  foreach (QAction *a, ui->mainToolBar->actions())
  {
    if (a->text() == tr("Languages"))
      a->setIcon(el->icon);
  }
}

void MainWindow::on_listWidget_itemClicked(QListWidgetItem *item)
{
  current_item = item;
}

void MainWindow::on_actionSaveProject_triggered()
{
  QString projectPath = project.GetCurrentPath();
  if (projectPath == "")
  {
    on_actionSave_Project_As_triggered();
  }
  else
  {
    SaveProject(projectPath);
  }
}

void MainWindow::on_actionLoadProject_triggered()
{
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open Laigter Project"), "",
      tr("Project File (*.laigter)"));
  if (fileName == "")
  {
    return;
  }
  LoadProject(fileName);
}

void MainWindow::LoadProject(QString path){
  QList <ImageProcessor *> newList;
  QJsonObject general_settings;
  /* Remove Current Processors */
  foreach (ImageProcessor *p, processorList)
  {
    remove_processor(p);
  }

  processorList.clear();

  project.load(path, &newList, &general_settings);

  /* Add processors from project */
  foreach (ImageProcessor *p, newList)
  {
    add_processor(p);
  }
  general_settings = general_settings.value("general").toObject();
  /* Apply general settings */
  ui->horizontalSliderAmbientLight->setValue(general_settings.value("ambient light").toInt());
  ui->blendSlider->setValue(general_settings.value("blend").toInt());
  ui->checkBoxLightsPerTexture->setChecked(general_settings.value("lights per texture").toBool());
  ui->checkBoxPixelated->setChecked(general_settings.value("pixelated").toBool());
  ui->checkBoxToon->setChecked(general_settings.value("toon").toBool());
  ui->comboBoxView->setCurrentIndex(general_settings.value("viewmode").toInt());

  /* Apply sample lights */
  QJsonArray lights = general_settings.value("sample lights").toArray();
  sample_processor->get_light_list_ptr()->clear();
  for (int i = 0; i < lights.count(); i++)
  {
    LightSource *light = new LightSource;
    QJsonObject light_json = lights.at(i).toObject();
    QJsonObject position_json = light_json.value("position").toObject();
    QVector3D light_position(position_json.value("x").toDouble(), position_json.value("y").toDouble(), position_json.value("z").toDouble());
    light->set_light_position(light_position);
    QJsonObject color_json = light_json.value("diffuse color").toObject();
    QColor diffuse(color_json.value("r").toInt(), color_json.value("g").toInt(), color_json.value("b").toInt());
    light->set_diffuse_color(diffuse);
    /* Change if plan to support different specular color */
    light->set_specular_color(diffuse);
    light->set_specular_scatter(light_json.value("specular scatter").toDouble());
    light->set_specular_intensity(light_json.value("specular intensity").toDouble());
    light->set_diffuse_intensity(light_json.value("diffuse intensity").toDouble());
    sample_processor->get_light_list_ptr()->append(light);
  }
}

void MainWindow::SaveProject(QString path)
{
  QJsonObject general_settings;
  general_settings.insert("viewmode", ui->comboBoxView->currentIndex());
  general_settings.insert("toon", ui->checkBoxToon->isChecked());
  general_settings.insert("pixelated", ui->checkBoxPixelated->isChecked());
  general_settings.insert("blend",ui->blendSlider->value());
  general_settings.insert("lights per texture", ui->checkBoxLightsPerTexture->isChecked());

  QJsonArray sample_lights;

  foreach (LightSource *light, *sample_processor->get_light_list_ptr())
  {
    QJsonObject light_props;
    QJsonObject light_position;
    QVector3D position = light->get_light_position();
    light_position.insert("x", position.x());
    light_position.insert("y", position.y());
    light_position.insert("z", position.z());
    light_props.insert("position", light_position);

    QJsonObject light_color;
    QColor color = light->get_diffuse_color();
    light_color.insert("r", color.red());
    light_color.insert("g", color.green());
    light_color.insert("b", color.blue());
    light_props.insert("diffuse color", light_color);
    /* if we get back the option to change specular color, this should be added here */
    light_props.insert("specular color", light_color);

    light_props.insert("diffuse intensity", light->get_diffuse_intensity());
    light_props.insert("specular intensity", light->get_specular_intesity());
    light_props.insert("specular scatter", light->get_specular_scatter());

    sample_lights.append(light_props);

  }
  general_settings.insert("sample lights", sample_lights);
  general_settings.insert("ambient light", ui->horizontalSliderAmbientLight->value());
  QList<ImageProcessor*> p_list;
  for (int i = 0; i < ui->listWidget->count(); i++)
  {
    p_list.append(find_processor(ui->listWidget->item(i)->text()));
  }
  project.save(path, p_list, general_settings);
}

void MainWindow::on_blendSlider_valueChanged(int value)
{
  ui->openGLPreviewWidget->blend_factor = value;
  ui->openGLPreviewWidget->need_to_update = true;
}



void MainWindow::on_actionSave_Project_As_triggered()
{
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Image"), "", tr("Image File (*.laigter)"));
  if (fileName == "")
    return;
  if (!fileName.endsWith(".laigter"))
  {
    fileName += ".laigter";
  }

  SaveProject(fileName);
}
