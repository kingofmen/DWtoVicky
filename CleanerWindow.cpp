#include "CleanerWindow.hh"
#include <QPainter> 
#include "Parser.hh"
#include <cstdio> 
#include <QtGui>
#include <QDesktopWidget>
#include <QRect>
#include <qpushbutton.h>
#include <iostream> 
#include <fstream> 
#include <string>
#include "Logger.hh" 
#include <set>
#include <list> 
#include <algorithm>
#include "StructUtils.hh" 
#include "boost/tokenizer.hpp"

#include <Windows.h>

bool fullPopWeightDebug = false; 
Object* debugObject = 0; 

using namespace std; 

char strbuffer[10000]; 
map<string, WorkerThread::ProvinceGroup*> WorkerThread::ProvinceGroup::allGroups; 
WorkerThread* WorkerThread::ProvinceGroup::boss = 0; 
int WorkerThread::PopDebug = 10; 

int main (int argc, char** argv) {
  QApplication industryApp(argc, argv);
  QDesktopWidget* desk = QApplication::desktop();
  QRect scr = desk->availableGeometry();
  CleanerWindow window;
  window.show();
  
  window.resize(3*scr.width()/5, scr.height()/2);
  window.move(scr.width()/5, scr.height()/4);
  window.setWindowTitle(QApplication::translate("toplevel", "DW converter"));
 
  QMenuBar* menuBar = window.menuBar();
  QMenu* fileMenu = menuBar->addMenu("File");
  QAction* newGame = fileMenu->addAction("Load file");
  QAction* trade   = fileMenu->addAction("Trade Stats");
  QAction* quit    = fileMenu->addAction("Quit");

  QObject::connect(quit,    SIGNAL(triggered()), &window, SLOT(close())); 
  QObject::connect(newGame, SIGNAL(triggered()), &window, SLOT(loadFile()));
  QObject::connect(trade,   SIGNAL(triggered()), &window, SLOT(tradeStats()));   

  QMenu* actionMenu = menuBar->addMenu("Actions");
  QAction* clean = actionMenu->addAction("Clean");
  QObject::connect(clean, SIGNAL(triggered()), &window, SLOT(cleanFile())); 
  QAction* stats = actionMenu->addAction("Stats");
  QObject::connect(stats, SIGNAL(triggered()), &window, SLOT(getStats()));
  QAction* convert = actionMenu->addAction("Convert");
  QObject::connect(convert, SIGNAL(triggered()), &window, SLOT(convert()));
  QAction* simplify = actionMenu->addAction("Simplify");
  QObject::connect(simplify, SIGNAL(triggered()), &window, SLOT(simplify())); 
  QAction* messSize = actionMenu->addAction("Mess with size");
  QObject::connect(messSize, SIGNAL(triggered()), &window, SLOT(messWithSize())); 
  
  window.textWindow = new QPlainTextEdit(&window);
  window.textWindow->setFixedSize(3*scr.width()/5 - 10, scr.height()/2-40);
  window.textWindow->move(5, 30);
  window.textWindow->show(); 

  Logger::createStream(Logger::Debug);
  Logger::createStream(Logger::Trace);
  Logger::createStream(Logger::Game);
  Logger::createStream(Logger::Warning);
  Logger::createStream(Logger::Error);
  Logger::createStream(WorkerThread::PopDebug); 

  QObject::connect(&(Logger::logStream(Logger::Debug)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Trace)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Game)),    SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Warning)), SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Error)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(WorkerThread::PopDebug)),   SIGNAL(message(QString)), &window, SLOT(message(QString))); 
  
  window.show();

  if (argc > 1) window.loadFile(argv[1], argc > 2 ? atoi(argv[2]) : 1);  
  
  return industryApp.exec();  
}


CleanerWindow::CleanerWindow (QWidget* parent) 
  : QMainWindow(parent)
  , worker(0)
{}

CleanerWindow::~CleanerWindow () {}

void CleanerWindow::message (QString m) {
  textWindow->appendPlainText(m); 
}

void CleanerWindow::loadFile (string fname, int autoTask) {
  if (worker) delete worker;
  worker = new WorkerThread(fname, autoTask);
  worker->start();
}

void CleanerWindow::loadFile () {
  QString filename = QFileDialog::getOpenFileName(this, tr("Select file"), QString(""), QString("*.eu3"));
  string fn(filename.toAscii().data());
  if (fn == "") return;
  loadFile(fn);   
}

void CleanerWindow::cleanFile () {
  Logger::logStream(Logger::Game) << "Starting clean.\n";
  worker->setTask(WorkerThread::CleanFile); 
  worker->start(); 
}

void CleanerWindow::getStats () {
  Logger::logStream(Logger::Game) << "Starting statistics.\n";
  worker->setTask(WorkerThread::Statistics); 
  worker->start(); 
}

void CleanerWindow::messWithSize () {
  Logger::logStream(Logger::Game) << "Looking at population size.\n";
  if (!worker) worker = new WorkerThread("nofile");
  worker->setTask(WorkerThread::MessWithSize); 
  worker->start(); 
}

void CleanerWindow::tradeStats () {
  Logger::logStream(Logger::Game) << "Looking at trade stats.\n";
  if (!worker) worker = new WorkerThread("nofile");
  worker->setTask(WorkerThread::TradeStats); 
  worker->start(); 
}

void CleanerWindow::convert () {
  Logger::logStream(Logger::Game) << "Convert.\n";
  worker->setTask(WorkerThread::Convert); 
  worker->start(); 
}

void CleanerWindow::simplify () {
  Logger::logStream(Logger::Game) << "Simplifying.\n";
  worker->setTask(WorkerThread::Simplify); 
  worker->start(); 
}

string addQuotes (string tag) {
  string ret("\"");
  ret += tag;
  ret += "\"";
  return ret; 
}

string remQuotes (string tag) {
  // Returns characters between first and second double quote
  // in tag; or tag if there are no double quotes. 
  
  string ret;
  bool found = false; 
  for (unsigned int i = 0; i < tag.size(); ++i) {
    char curr = tag[i];
    if (!found) {
      if (curr != '"') continue;
      found = true;
      continue;
    }

    if (curr == '"') break;
    ret += curr; 
  }

  if (!found) return tag; 
  return ret; 
}

WorkerThread::WorkerThread (string fn, int aTask)
  : targetVersion(".\\DW\\")
  , fname(fn)
  , eu3Game(0)
  , vicGame(0)
  , task(LoadFile)
  , configObject(0)
  , histPopFraction(0.5)
  , cachedPopWeights(0)
  , autoTask(aTask)
{
  WorkerThread::ProvinceGroup::boss = this;
}  

WorkerThread::~WorkerThread () {
  if (eu3Game) delete eu3Game;
  if (vicGame) delete vicGame; 
  eu3Game = 0;
  vicGame = 0; 
}

void WorkerThread::run () {
  if (!configObject) configure();
    
  switch (task) {
  case LoadFile: loadFile(fname); break;
  case CleanFile: cleanFile(); break;
  case Statistics: getStatistics(); break;
  case Convert: convert(); break;
  case MessWithSize: messWithSize(); break;
  case Simplify: simplify(); break;        
  case TradeStats: tradeStats(); break; 
  case NumTasks: 
  default: break; 
  }
}

int WorkerThread::level (Object* building) {
  string prev = building->safeGetString("previous", "bah");
  if (prev == "bah") return 1;
  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
    if ((*b)->getKey() != prev) continue;
    return 1 + level(*b);
  }
  assert(false); 
  return 0; 
}

int WorkerThread::category (Object* building) {
  int cat = building->safeGetInt("category", -1);
  if (-1 != cat) return cat;
  string prev = building->safeGetString("previous", "bah");
  if (prev == "bah") return -1;
  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
    if ((*b)->getKey() != prev) continue;
    return category(*b);
  }
  return -1; 
}

struct CountryStat {
  CountryStat (); 
  
  int ownedProvinces;
  int basetax;
  vector<double> buildings;
  vector<double> maxLevels;
  double fullyDeveloped;
  double magistrates;
  QPixmap* flag; 
};

struct HistogramBin {
  void insert (CountryStat* c, double val);
  
  list<pair<double, CountryStat*> > entries;
  double upper; 
};

void HistogramBin::insert (CountryStat* c, double val) {
  if ((0 == entries.size()) || (val > entries.back().first)) {
    entries.push_back(pair<double, CountryStat*>(val, c));
    return; 
  }
  for (list<pair<double, CountryStat*> >::iterator i = entries.begin(); i != entries.end(); ++i) {
    if (val > (*i).first) continue;
    entries.insert(i, pair<double, CountryStat*>(val, c));
    break; 
  }
}

struct Histogram {
  Histogram (int nb, double lo, double up); 
  void fill (CountryStat* dat, double value); 
  void draw (const char* fname); 
    
  vector<HistogramBin*> bins;
  double lowerLimit;
  double upperLimit;
  int numBins;
  unsigned int maxEntries; 
};

Histogram::Histogram (int nb, double lo, double up)
  : lowerLimit(lo)
  , upperLimit(up)
  , numBins(nb)
  , maxEntries(0)
{
  double step = (upperLimit - lowerLimit) / numBins; 
  for (int i = 0; i < numBins; ++i) {
    HistogramBin* curr = new HistogramBin();
    curr->upper = lowerLimit + (1 + i)*step;
    bins.push_back(curr);
  }
}

void Histogram::draw (const char* fname) {
  int width = 30+numBins*32;
  int height = 10+max((numBins+1)*20, (2 + (int)maxEntries)*20);
  QImage image(width, height, QImage::Format_RGB32);
  QPainter painter(&image);
  QPen pen(Qt::white);
  QBrush brush;
  brush.setStyle(Qt::SolidPattern);
  brush.setColor(Qt::white); 
  painter.setPen(pen);
  painter.setBrush(brush); 
  painter.drawRect(0, 0, 500, 300);
  pen.setColor(Qt::black);
  painter.setPen(pen);
  for (int i = 0; i < numBins; ++i) {
    int xpos = 15 + 32*i;
    int ypos = height - 30;
    for (list<pair<double, CountryStat*> >::iterator e = bins[i]->entries.begin(); e != bins[i]->entries.end(); ++e) {
      ypos -= 20; 
      painter.drawPixmap(QRect(xpos, ypos, 32, 20), *((*e).second->flag), QRect(0, 0, 64, 64)); 
    }
    sprintf(strbuffer, "%.0f", bins[i]->upper); 
    painter.drawText(xpos + 24, height - 10, strbuffer);
  }
  sprintf(strbuffer, "%.0f", lowerLimit); 
  painter.drawText(7, height - 10, strbuffer);
  
  painter.drawLine(15, height-30, width-15, height - 30);
  painter.drawLine(15, height-30, 15, 20); 
  image.save(fname); 
}

void Histogram::fill (CountryStat* dat, double value) {
  if (value < lowerLimit) return;
  if (value > upperLimit) return;

  for (int i = 0; i < numBins; ++i) {
    if (value > bins[i]->upper) continue;
    bins[i]->insert(dat, value);
    maxEntries = max(bins[i]->entries.size(), maxEntries); 
    break; 
  }
}

CountryStat::CountryStat ()
  : ownedProvinces(0)
  , basetax(0)
  , buildings()
  , maxLevels()
  , fullyDeveloped(0)
  , magistrates(0)
{
  buildings.resize(10);
  maxLevels.resize(10);
}

void addFlagToImage (QPixmap* flag, QPainter* painter, double dl, double dr, double up) {
  int xpos = 250;
  int ypos = 315;
  double scale = 1.0 / (dl + dr + up);
  xpos -= (int) floor(scale * dl * 240 + 0.5);
  xpos += (int) floor(scale * dr * 240 + 0.5);
  ypos -= (int) floor(scale * up * 277 + 0.5);
  ypos += (int) floor(scale * dl * 138 + 0.5); 
  ypos += (int) floor(scale * dr * 138 + 0.5);
  
  painter->drawPixmap(QRect(xpos - 16, ypos - 10, 32, 20), *flag, QRect(0, 0, 64, 64));
}

pair<QPainter*, QImage*> makePainter (const char* dl, const char* dr, const char* up) {
  QImage* image = new QImage(500, 500, QImage::Format_RGB32);
  QPainter* painter = new QPainter(image);
  QPen pen(Qt::white);
  QBrush brush;
  brush.setStyle(Qt::SolidPattern);
  brush.setColor(Qt::white); 
  painter->setPen(pen);
  painter->setBrush(brush); 
  QPolygon triangle(3); 
  triangle.putPoints(0, 3, 10, 454, 490, 454, 250, 46);
  painter->drawPolygon(triangle);
  painter->drawText(5, 484, dl);
  painter->drawText(470, 484, dr);
  painter->drawText(235, 16, up);
  pen.setColor(Qt::black);
  painter->setPen(pen);
  painter->drawLine(250, 315, 250,  46);
  painter->drawLine(250, 315,  10, 454);
  painter->drawLine(250, 315, 490, 454);
  return pair<QPainter*, QImage*>(painter, image); 
}

string unitType (string regtype) {
  // Lists are incomplete. 
  if      (regtype == "gaelic_free_shooter") return "infantry";
  else if (regtype == "aztec_hill_warfare") return "infantry";
  else if (regtype == "irish_charge") return "infantry";
  else if (regtype == "spanish_tercio") return "infantry";
  else if (regtype == "eastern_medieval_infantry") return "infantry";
  else if (regtype == "western_medieval_infantry") return "infantry";  
  else if (regtype == "bardiche_infantry") return "infantry";
  else if (regtype == "western_men_at_arms") return "infantry";
  else if (regtype == "swiss_landsknechten") return "infantry";
  else if (regtype == "halberd_infantry") return "infantry";
  else if (regtype == "ottoman_yaya") return "infantry";
  else if (regtype == "dutch_maurician") return "infantry";
  else if (regtype == "mamluk_archer") return "infantry";
  else if (regtype == "persian_footsoldier") return "infantry";
  else if (regtype == "tofongchis_musketeer") return "infantry";
  else if (regtype == "mongol_bow") return "infantry"; 
  else if (regtype == "muscovite_musketeer") return "infantry";
  else if (regtype == "native_indian_archer") return "infantry";
  else if (regtype == "mamluk_duel") return "infantry";
  else if (regtype == "chinese_longspear") return "infantry";
  else if (regtype == "ottoman_sekban") return "infantry";
  else if (regtype == "anglofrench_line") return "infantry";
  else if (regtype == "swedish_gustavian") return "infantry";
  else if (regtype == "swedish_caroline") return "infantry";
  else if (regtype == "austrian_grenzer") return "infantry";
  else if (regtype == "scottish_highlander") return "infantry";
  else if (regtype == "ottoman_nizami_cedid") return "infantry";
  else if (regtype == "british_redcoat") return "infantry";
  else if (regtype == "napoleonic_square") return "infantry";
  else if (regtype == "mixed_order_infantry") return "infantry";
  else if (regtype == "french_bluecoat") return "infantry";
  //else if (regtype == "") return "infantry";
  else if (regtype == "chambered_demi_cannon") return "artillery";
  else if (regtype == "large_cast_bronze_mortar") return "artillery";
  else if (regtype == "culverin") return "artillery";
  else if (regtype == "pedrero") return "artillery";
  else if (regtype == "large_cast_iron_bombard") return "artillery";
  else if (regtype == "coehorn_mortar") return "artillery";
  else if (regtype == "swivel_cannon") return "artillery";
  else if (regtype == "royal_mortar") return "artillery";
  else if (regtype == "flying_battery") return "artillery";
  //else if (regtype == "") return "artillery";
  //else if (regtype == "") return "artillery";
  else if (regtype == "qizilbash_cavalry") return "cavalry";
  else if (regtype == "french_caracolle") return "cavalry";
  else if (regtype == "eastern_knights") return "cavalry";
  else if (regtype == "chevauchee") return "cavalry";
  else if (regtype == "western_medieval_knights") return "cavalry";
  else if (regtype == "ottoman_musellem") return "cavalry";
  else if (regtype == "mamluk_cavalry_charge") return "cavalry";
  else if (regtype == "muslim_cavalry_archers") return "cavalry";
  else if (regtype == "druzhina_cavalry") return "cavalry";
  else if (regtype == "persian_cavalry_charge") return "cavalry";
  else if (regtype == "topchis_artillery") return "cavalry";
  else if (regtype == "shaybani") return "cavalry";
  else if (regtype == "rajput_hill_fighters") return "cavalry";
  else if (regtype == "ottoman_spahi") return "cavalry";
  else if (regtype == "ottoman_reformed_spahi") return "cavalry";
  else if (regtype == "austrian_hussar") return "cavalry";
  else if (regtype == "swedish_arme_blanche") return "cavalry";  
  else if (regtype == "swedish_galoop") return "cavalry";
  else if (regtype == "open_order_cavalry") return "cavalry";
  else if (regtype == "napoleonic_lancers") return "cavalry";
  else if (regtype == "french_cuirassier") return "cavalry";
  //else if (regtype == "") return "cavalry";
  //else if (regtype == "") return "cavalry";
  else if (regtype == "caravel") return "big_ship";
  else if (regtype == "wargalleon") return "big_ship";
  else if (regtype == "galleon") return "big_ship";
  else if (regtype == "carrack") return "big_ship";
  else if (regtype == "twodecker") return "big_ship";
  else if (regtype == "threedecker") return "big_ship";
  //else if (regtype == "") return "big_ship";    
  else if (regtype == "frigate") return "light_ship";
  else if (regtype == "early_frigate") return "light_ship";
  else if (regtype == "barque") return "light_ship";
  else if (regtype == "heavy_frigate") return "light_ship";
  //else if (regtype == "") return "light_ship";
  //else if (regtype == "") return "light_ship";  
  else if (regtype == "galley") return "galley";
  else if (regtype == "galleass") return "galley";
  else if (regtype == "chebeck") return "galley";
  else if (regtype == "archipelago_frigate") return "galley";
  //else if (regtype == "") return "galley";
  //else if (regtype == "") return "galley";
  //else if (regtype == "") return "galley";
  //else if (regtype == "") return "galley";
  else if (regtype == "merchantman") return "transport";
  else if (regtype == "flute") return "transport";
  else if (regtype == "cog") return "transport";
  else if (regtype == "eastindiaman") return "transport";
  //else if (regtype == "") return "transport";
  //else if (regtype == "") return "transport";
  //else if (regtype == "") return "transport";

  Logger::logStream(Logger::Game) << "Unknown unit type " << regtype << "\n"; 
  return "unknown"; 
}

void row (ofstream& writer, string rowname, vector<string>& entries) {
  unsigned int columns = 100;
  static const string startTD = "<td style=\"text-align:center;padding:10\">";
  writer << "<tr>";
  for (unsigned int i = 0; i < entries.size(); ++i) {
    if (0 == i%columns) writer << startTD << rowname << "</td>";
    writer << startTD << entries[i] << "</td>";
  }
  writer << startTD << rowname << "</td></tr>\n";
}

void makeRow (ofstream& writer, string rowname, string keyword, objvec& countries) {
  vector<string> entries;
  for (objiter c = countries.begin(); c != countries.end(); ++c) {
    entries.push_back((*c)->safeGetString(keyword, "0")); 
  }
  row(writer, rowname, entries); 
}

void WorkerThread::getStatistics () {
  if (!eu3Game) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  createCountryMappings(); // Need this for the findEu3CountryByTag calls used below. 
  
  if (configObject->safeGetString("do_pop_stats", "no") == "yes") {
    double totalPop = 0; 
    for (int i = 0; i < 2000; ++i) {
      sprintf(strbuffer, "%i", i);
      string provnum(strbuffer);
      Object* prov = eu3Game->safeGetObject(provnum);
      if (!prov) continue;
      totalPop += prov->safeGetFloat("citysize");
    }
    Logger::logStream(Logger::Game) << "Total population: " << totalPop << ".\n"; 
  }
  if (configObject->safeGetString("do_trade_stats", "no") == "yes") {
    Object* dummyTotal = new Object("TOT"); 
    Object* trade = eu3Game->safeGetObject("trade");
    objvec cots = trade->getValue("cot");
    objvec countryList; 
    map<Object*, map<Object*, double> > tradeMap; 
    for (objiter cot = cots.begin(); cot != cots.end(); ++cot) {
      string provtag = (*cot)->safeGetString("location");
      Object* eu3province = eu3Game->safeGetObject(provtag);
      Object* owner = eu3Game->safeGetObject(remQuotes(eu3province->safeGetString("owner")));
      if (!owner) continue;
      if (owner->safeGetString("human", "no") != "yes") continue;
      
      if (find(countryList.begin(), countryList.end(), owner) == countryList.end()) countryList.push_back(owner); 
      
      objvec traders = (*cot)->getLeaves();
      for (objiter trader = traders.begin(); trader != traders.end(); ++trader) {
	double value = (*trader)->safeGetFloat("value", -1);
	if (0 > value) continue;
	Object* country = eu3Game->safeGetObject((*trader)->getKey());
	if (!country) continue;
	if (country->safeGetString("human", "no") != "yes") continue; 
	tradeMap[country][owner] += value;
	tradeMap[dummyTotal][owner] += value;
	tradeMap[country][dummyTotal] += value; 
      }
    }
    countryList.push_back(dummyTotal); 
    
    Logger::logStream(Logger::Game) << "     ";
    for (objiter country = countryList.begin(); country != countryList.end(); ++country) {
      Logger::logStream(Logger::Game) << " " << (*country)->getKey() << "  ";
    }
    
    Logger::logStream(Logger::Game) << "\n";
    
    for (objiter country = countryList.begin(); country != countryList.end(); ++country) {
      Logger::logStream(Logger::Game) << (*country)->getKey() << "  ";
      for (objiter owner = countryList.begin(); owner != countryList.end(); ++owner) {
	sprintf(strbuffer, "%4i", (int) floor(0.5+tradeMap[*country][*owner])); 
	Logger::logStream(Logger::Game) << strbuffer << "  ";
      }
      Logger::logStream(Logger::Game) << (*country)->getKey() << "  \n";
    }
  }

  if (configObject->safeGetString("do_building_stats", "no") == "yes") {
    objvec allEu3Objects = eu3Game->getLeaves();
    map<Object*, CountryStat*> statMap; 
    for (objiter prov = allEu3Objects.begin(); prov != allEu3Objects.end(); ++prov) {
      string ownerTag = (*prov)->safeGetString("owner", "NONE");
      if (ownerTag == "NONE") continue;
      Object* owner = findEu3CountryByTag(ownerTag);
      if (!owner) continue;
      if (owner->safeGetString("human", "no") != "yes") continue;
      if (!statMap[owner]) statMap[owner] = new CountryStat();
      statMap[owner]->ownedProvinces++;
      statMap[owner]->basetax += (*prov)->safeGetFloat("base_tax");
      for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
	if (!hasBuildingOrBetter((*b)->getKey(), (*prov))) continue;
	int cat = category(*b);
	if (-1 == cat) continue;
	
	statMap[owner]->buildings[cat]++; 
	if (5 == level(*b)) {
	  statMap[owner]->maxLevels[cat]++;
	  statMap[owner]->fullyDeveloped++;
	}
	statMap[owner]->magistrates++; 
      }
    }
    
    Logger::logStream(Logger::Game) << "\n\n";
    Logger::logStream(Logger::Game) << "TAG\tProvs\tMags\tM/P\tBase\tM/B\tFull\tGovt\tGovtMax\tArmy\tArmyMax\tNavy\tNavyMax\tFort\tFortMax\tProd\tProdMax\tTrad\tTradMax\n";
    Logger::logStream(Logger::Game).setPrecision(2); 
    for (map<Object*, CountryStat*>::iterator c = statMap.begin(); c != statMap.end(); ++c) {
      CountryStat* curr = (*c).second;
      Logger::logStream(Logger::Game) << (*c).first->getKey() << ":\t"
				      << curr->ownedProvinces << "\t"
				      << curr->magistrates << "\t"
				      << (curr->magistrates / curr->ownedProvinces) << "\t"
				      << curr->basetax << "\t"
				      << (curr->magistrates / curr->basetax) << "\t"
				      << (curr->fullyDeveloped / curr->ownedProvinces) << "\t";
      for (int i = 1; i < 7; ++i) {
	Logger::logStream(Logger::Game) << (curr->buildings[i] / curr->ownedProvinces) << "\t"
					<< (curr->maxLevels[i] / curr->ownedProvinces) << "\t";
      }
      Logger::logStream(Logger::Game) << "\n";     
    }
    Logger::logStream(Logger::Game).setPrecision(-1); 
    
    Histogram magsPerProv(10, 6, 16);
    Histogram magsTotal(10, 300, 1300);
    Histogram magsPerTax(10, 0, 5);
    Histogram provs(10, 30, 160);
    Histogram baset(10, 0, 1000); 
    
    pair<QPainter*, QImage*> army_navy_othr = makePainter("Army", "Navy", "Other");
    pair<QPainter*, QImage*> milt_govt_econ = makePainter("Military", "Govt", "Econ");
    pair<QPainter*, QImage*> prod_govt_trad = makePainter("Production", "Govt", "Trade");
    pair<QPainter*, QImage*> milt_othr_unco = makePainter("Military", "Other", "Unused");
    
    pair<QPainter*, QImage*> mag_army_navy_othr = makePainter("Army", "Navy", "Other");
    pair<QPainter*, QImage*> mag_milt_govt_econ = makePainter("Military", "Govt", "Econ");
    pair<QPainter*, QImage*> mag_prod_govt_trad = makePainter("Production", "Govt", "Trade");
    
    for (map<Object*, CountryStat*>::iterator c = statMap.begin(); c != statMap.end(); ++c) {
      CountryStat* curr = (*c).second;
      string flagfile = "./flags/";
      flagfile += (*c).first->getKey();
      flagfile += ".bmp";
      curr->flag = new QPixmap(flagfile.c_str());
      
      double govt = (curr->maxLevels[1] / curr->ownedProvinces);
      double army = (curr->maxLevels[2] / curr->ownedProvinces);
      double navy = (curr->maxLevels[3] / curr->ownedProvinces);
      double fort = (curr->maxLevels[4] / curr->ownedProvinces);
      double prod = (curr->maxLevels[5] / curr->ownedProvinces);
      double trad = (curr->maxLevels[6] / curr->ownedProvinces);
      
      double othr = prod + trad + govt; 
      double milt = fort + army + navy;
      double econ = prod + trad; 
      double unco = (1.0 - prod - trad - govt - army - navy - fort); 
      
      addFlagToImage(curr->flag, army_navy_othr.first, army, navy, othr);
      addFlagToImage(curr->flag, milt_govt_econ.first, milt, govt, econ);
      addFlagToImage(curr->flag, prod_govt_trad.first, prod, govt, trad);
      addFlagToImage(curr->flag, milt_othr_unco.first, milt, othr, unco);
      
      govt = (curr->buildings[1] / curr->ownedProvinces);
      army = (curr->buildings[2] / curr->ownedProvinces);
      navy = (curr->buildings[3] / curr->ownedProvinces);
      fort = (curr->buildings[4] / curr->ownedProvinces);
      prod = (curr->buildings[5] / curr->ownedProvinces);
      trad = (curr->buildings[6] / curr->ownedProvinces);
      
      othr = prod + trad + govt; 
      milt = fort + army + navy;
      econ = prod + trad; 
      
      addFlagToImage(curr->flag, mag_army_navy_othr.first, army, navy, othr);
      addFlagToImage(curr->flag, mag_milt_govt_econ.first, milt, govt, econ);
      addFlagToImage(curr->flag, mag_prod_govt_trad.first, prod, govt, trad);
      
      magsPerProv.fill(curr, curr->magistrates / curr->ownedProvinces);
      magsTotal.fill(curr, curr->magistrates);
      magsPerTax.fill(curr, (curr->magistrates / curr->basetax));
      provs.fill(curr, curr->ownedProvinces);
      baset.fill(curr, curr->basetax); 
    }
    
    army_navy_othr.second->save("army_navy_othr.png");
    milt_govt_econ.second->save("milt_govt_econ.png");
    prod_govt_trad.second->save("prod_govt_trad.png");
    milt_othr_unco.second->save("milt_othr_unco.png");
    mag_army_navy_othr.second->save("mag_army_navy_othr.png");
    mag_milt_govt_econ.second->save("mag_milt_govt_econ.png");
    mag_prod_govt_trad.second->save("mag_prod_govt_trad.png");
    
    magsPerProv.draw("magsPerProv.png");
    magsTotal.draw("magsTotal.png");
    magsPerTax.draw("magsPerBasetax.png");
    provs.draw("provinces.png");
    baset.draw("basetax.png"); 
  }
  
  // Battle statistics
  objvec extraInterest;
  Object* cExtra = configObject->getNeededObject("extraInterest");
  objvec extras = cExtra->getLeaves();
  for (objiter e = extras.begin(); e != extras.end(); ++e) {
    Object* extra = findEu3CountryByTag((*e)->getLeaf());
    if (!extra) continue;
    extraInterest.push_back(extra); 
  }
  Object* warAndPeace = new Object("warAndPeace");

  objvec oldWars = eu3Game->getValue("previous_war");
  objvec wars = eu3Game->getValue("active_war");
  for (objiter w = oldWars.begin(); w != oldWars.end(); ++w) wars.push_back(*w);
  map<Object*, BattleInfo*> landBattleMap;
  map<Object*, BattleInfo*> navyBattleMap;
  for (objiter war = wars.begin(); war != wars.end(); ++war) {
    Object* history = (*war)->safeGetObject("history");
    if (!history) continue;
    int smallestYear = 100000;
    int largestYear = 0;
    int casualties = 0; 
    objvec dates = history->getLeaves();
    for (objiter date = dates.begin(); date != dates.end(); ++date) {
      int year = 0;
      days((*date)->getKey(), &year); 
      Object* battle = (*date)->safeGetObject("battle");
      if (!battle) continue;
      if (year < smallestYear) smallestYear = year;
      if (year > largestYear)  largestYear  = year; 
      
      BattleInfo* attackInfo = 0;
      BattleInfo* defendInfo = 0;      
      Object* aCountry = 0;
      Object* dCountry = 0; 
      bool naval = false; 
      
      Object* attacker = battle->safeGetObject("attacker");
      if (attacker) {
	aCountry = findEu3CountryByTag(remQuotes(attacker->safeGetString("country", "\"NONE\"")));
	naval             = (attacker->safeGetInt("big_ship")   > 0);
	if (!naval) naval = (attacker->safeGetInt("light_ship") > 0);
	if (!naval) naval = (attacker->safeGetInt("transport")  > 0);
	if (!naval) naval = (attacker->safeGetInt("galley")     > 0);	
      }
      if (aCountry) {
	attackInfo = naval ? navyBattleMap[aCountry] : landBattleMap[aCountry];
	if (!attackInfo) {
	  attackInfo = new BattleInfo();
	  if (naval) navyBattleMap[aCountry] = attackInfo;
	  else       landBattleMap[aCountry] = attackInfo;
	}
      }

      Object* defender = battle->safeGetObject("defender");
      if (defender) dCountry = findEu3CountryByTag(defender->safeGetString("country", "NONE"));
      if (dCountry) {
	defendInfo = naval ? navyBattleMap[dCountry] : landBattleMap[dCountry];
	if (!defendInfo) {
	  defendInfo = new BattleInfo();
	  if (naval) navyBattleMap[dCountry] = defendInfo;
	  else       landBattleMap[dCountry] = defendInfo;
	}
      }

      if (attackInfo) casualties += attackInfo->addBattle(attacker, defender, year);
      if (defendInfo) casualties += defendInfo->addBattle(defender, attacker, year);
    }
    if ((largestYear != smallestYear) && (100000 != smallestYear)) {
      Object* currWar = new Object("war");
      currWar->setLeaf("begin", smallestYear);
      currWar->setLeaf("end", largestYear);
      currWar->setLeaf("killed", casualties);
      currWar->setLeaf("name", (*war)->safeGetString("name"));
      warAndPeace->setValue(currWar); 
    }    
  }

  for (map<Object*, BattleInfo*>::iterator info = landBattleMap.begin(); info != landBattleMap.end(); ++info) {
    Object* country = (*info).first;
    bool human = (find(extraInterest.begin(), extraInterest.end(), country) != extraInterest.end());
    if (human) continue; 
    human = (country->safeGetString("human", "no") == "yes");
    if (!human) continue; 
    extraInterest.push_back(country); 
  }

  Logger::logStream(Logger::Game) << "\n\nKills, losses, ratio:\n";
  for (objiter c = extraInterest.begin(); c != extraInterest.end(); ++c) {
    Object* country = (*c);
    if (!landBattleMap[country]) continue;
    country->resetLeaf("ratio", landBattleMap[country]->ratio);
  }

  ObjectDescendingSorter ratioSorter("ratio");
  sort(extraInterest.begin(), extraInterest.end(), ratioSorter);
  for (objiter c = extraInterest.begin(); c != extraInterest.end(); ++c) {
    Object* country = (*c);
    if (!landBattleMap[country]) continue;
    Logger::logStream(Logger::Game) << country->getKey() << " : "
				    << landBattleMap[country]->killed << ", "
				    << landBattleMap[country]->losses << ", "
				    << landBattleMap[country]->ratio 
				    << "\n"; 
  }

  // Matrix of country-specific casualties (in thousands) 
  Logger::logStream(Logger::Game) << "\n\t";
  for (objiter killer = extraInterest.begin(); killer != extraInterest.end(); ++killer) {
    sprintf(strbuffer, "%s   ", (*killer)->getKey().c_str());
    Logger::logStream(Logger::Game) << strbuffer;
  }
  Logger::logStream(Logger::Game) << "\n"; 
  for (objiter killer = extraInterest.begin(); killer != extraInterest.end(); ++killer) {
    Logger::logStream(Logger::Game) << (*killer)->getKey() << "\t"; 
    for (objiter losser = extraInterest.begin(); losser != extraInterest.end(); ++losser) {
      sprintf(strbuffer, "%-6.1f", 0.001*landBattleMap[*killer]->killMap[(*losser)->getKey()]);
      Logger::logStream(Logger::Game) << strbuffer; 
    }
    Logger::logStream(Logger::Game) << "\n"; 
  }

  Logger::logStream(Logger::Game) << "\nGlobal losses by year:\n"; 
  for (map<int, int>::iterator i = BattleInfo::globalYearlyLosses.begin(); i != BattleInfo::globalYearlyLosses.end(); ++i) {
    Logger::logStream(Logger::Game) << (*i).first << " : " << (*i).second << "\n"; 
  }

  ofstream writer;  
  writer.open(".\\Output\\wartime.txt");
  Parser::topLevel = warAndPeace;
  writer << (*warAndPeace);
  writer.close();
  Logger::logStream(Logger::Game) << "Wrote wartime file.\n"; 

  objvec allEu3Objects = eu3Game->getLeaves();
  for (objiter prov = allEu3Objects.begin(); prov != allEu3Objects.end(); ++prov) {
    string ownerTag = (*prov)->safeGetString("owner", "NONE");
    if (ownerTag == "NONE") continue;
    Object* owner = findEu3CountryByTag(ownerTag);
    if (!owner) continue;
    owner->resetLeaf("provinces", 1 + owner->safeGetInt("provinces"));
    owner->resetLeaf("totalBaseTax", owner->safeGetInt("totalBaseTax") + (int) floor((*prov)->safeGetFloat("base_tax") + 0.5));
    owner->resetLeaf("totalBaseMP", owner->safeGetInt("totalBaseMP") + (int) floor((*prov)->safeGetFloat("manpower") + 0.5));
    owner->resetLeaf("totalBasePop", owner->safeGetInt("totalBasePop") + (int) floor((*prov)->safeGetFloat("citysize") + 0.5));        
    for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
      if (!hasBuildingOrBetter((*b)->getKey(), (*prov))) continue;
      owner->resetLeaf("totalMagistrates", level(*b) + owner->safeGetInt("totalMagistrates"));
      owner->resetLeaf((*b)->getKey(), 1 + owner->safeGetInt((*b)->getKey())); 
    }
  }

  for (objiter country = extraInterest.begin(); country != extraInterest.end(); ++country) {
    objvec armies = (*country)->getValue("army");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regiments = (*army)->getValue("regiment");
      for (objiter regiment = regiments.begin(); regiment != regiments.end(); ++regiment) {
	string regtype = remQuotes((*regiment)->safeGetString("type"));
	regtype = unitType(regtype);
	if      (regtype == "infantry")  (*country)->resetLeaf("infRegiments", 1 + (*country)->safeGetInt("infRegiments"));
	else if (regtype == "cavalry")   (*country)->resetLeaf("cavRegiments", 1 + (*country)->safeGetInt("cavRegiments"));
	else if (regtype == "artillery") (*country)->resetLeaf("artRegiments", 1 + (*country)->safeGetInt("artRegiments"));
	else                             (*country)->resetLeaf("unkRegiments", 1 + (*country)->safeGetInt("unkRegiments"));
      }
    }
    objvec navies = (*country)->getValue("navy");
    for (objiter army = navies.begin(); army != navies.end(); ++army) {
      objvec ships = (*army)->getValue("ship");
      for (objiter ship = ships.begin(); ship != ships.end(); ++ship) {
	string regtype = remQuotes((*ship)->safeGetString("type"));
	regtype = unitType(regtype);
	if      (regtype == "big_ship")   (*country)->resetLeaf("bigships",   1 + (*country)->safeGetInt("bigships"));
	else if (regtype == "light_ship") (*country)->resetLeaf("lightships", 1 + (*country)->safeGetInt("lightships"));
	else if (regtype == "galley")     (*country)->resetLeaf("galleys",    1 + (*country)->safeGetInt("galleys"));
	else if (regtype == "transport")  (*country)->resetLeaf("transports", 1 + (*country)->safeGetInt("transports"));	
	else                              (*country)->resetLeaf("unkShips",   1 + (*country)->safeGetInt("unkShips"));
      }
    }    
  }
  
  
  writer.open(".\\Output\\table.html");
  writer << "<table border=\"1\" style=\"text-align:center\">\n";
  vector<string> names;
  for (objiter c = extraInterest.begin(); c != extraInterest.end(); ++c) names.push_back((*c)->getKey());
  row(writer, "", names);
  vector<string> current;
  makeRow(writer, "Plutocracy", "aristocracy_plutocracy", extraInterest);
  makeRow(writer, "Decentralisation", "centralization_decentralization", extraInterest);
  makeRow(writer, "Narrowminded", "innovative_narrowminded", extraInterest);
  makeRow(writer, "Free trade", "mercantilism_freetrade", extraInterest);
  makeRow(writer, "Defensive", "offensive_defensive", extraInterest);
  makeRow(writer, "Naval", "land_naval", extraInterest);
  makeRow(writer, "Quantity", "quality_quantity", extraInterest);
  makeRow(writer, "Free subjects", "serfdom_freesubjects", extraInterest);
  row(writer, "", names);
  makeRow(writer, "Provinces", "provinces", extraInterest);
  makeRow(writer, "Base tax", "totalBaseTax", extraInterest);
  makeRow(writer, "Base MP", "totalBaseMP", extraInterest);
  makeRow(writer, "Current MP", "manpower", extraInterest); 
  makeRow(writer, "Population", "totalBasePop", extraInterest);
  row(writer, "", names);  
  makeRow(writer, "Magistrates", "totalMagistrates", extraInterest);
  for (int i = 1; i <= 7; ++i) {
    for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
      if (category(*b) != i) continue; 
      makeRow(writer, (*b)->getKey(), (*b)->getKey(), extraInterest);
    }
  }
  row(writer, "", names);    
  makeRow(writer, "Infantry",        "infRegiments", extraInterest);
  makeRow(writer, "Cavalry",         "cavRegiments", extraInterest);
  makeRow(writer, "Artillery",       "artRegiments", extraInterest);
  makeRow(writer, "Big ships",       "bigships",     extraInterest);
  makeRow(writer, "Light ships",     "lightships",   extraInterest);
  makeRow(writer, "Galleys",         "galleys",      extraInterest);
  makeRow(writer, "Transports",      "transports",   extraInterest);
  makeRow(writer, "Infantry type",   "infantry",     extraInterest);
  makeRow(writer, "Cavalry type",    "cavalry",      extraInterest);
  makeRow(writer, "Artillery type",  "artillery",    extraInterest);
  makeRow(writer, "Big ship type",   "big_ship",     extraInterest);
  makeRow(writer, "Light ship type", "light_ship",   extraInterest);
  makeRow(writer, "Galley type",     "galley",       extraInterest);
  makeRow(writer, "Transport type",  "transport",    extraInterest);
  row(writer, "", names);    

  
  writer << "</table>\n";
  writer.close();
  Logger::logStream(Logger::Game) << "Wrote table file.\n";
  Logger::logStream(Logger::Game) << "Done with statistics.\n";
}

map<int, int> BattleInfo::globalYearlyLosses; 

int BattleInfo::addBattle (Object* ourSide, Object* theirSide, int year) {
  int ourLoss = 0;
  int thrLoss = 0;

  if (ourSide) {
    ourLoss = ourSide->safeGetInt("losses");
  }
  
  if (theirSide) {
    thrLoss = theirSide->safeGetInt("losses");
    killMap[remQuotes(theirSide->safeGetString("country"))] += thrLoss;
  }
  losses += ourLoss;
  killed += thrLoss; 
  

  ratio = killed / (1.0 + losses);
  yearlyLosses[year] += ourLoss;
  globalYearlyLosses[year] += ourLoss;
  return ourLoss; 
}

void WorkerThread::cleanFile () {
  if (!eu3Game) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  Logger::logStream(Logger::Game) << "Not implemented.\n";
  Logger::logStream(Logger::Game) << "Done cleaning.\n";
}

Object* WorkerThread::loadTextFile (string fname) {
  Logger::logStream(Logger::Game) << "Parsing file " << fname << "\n";
  ifstream reader;
  //string dummy;
  reader.open(fname.c_str());
  //reader >> dummy;
  if ((reader.eof()) || (reader.fail())) {
    Logger::logStream(Logger::Error) << "Could not open file \"" << fname << "\", returning null object.\n";
    return 0; 
  }
  
  Object* ret = processFile(fname);
  Logger::logStream(Logger::Game) << " ... done.\n";
  return ret; 
}

void WorkerThread::loadFile (string fname) {
  eu3Game = loadTextFile(fname);  
  Logger::logStream(Logger::Game) << "Done processing.\n";
  switch (autoTask) {
  case 1:
    task = Convert;
    convert(); 
    break;
  case 2:
    task = Statistics;
    getStatistics();
    break;
  default:
    break;
  }
}

WorkerThread::PopInfo::PopInfo (string p)
  : worldTotal(0)
  , integratedLiteracy(0) 
  , ptype(p)
{} 
  
WorkerThread::ProvinceGroup::ProvinceGroup () {
  for (map<string, Object*>::iterator name = boss->poptypes.begin(); name != boss->poptypes.end(); ++name) {
    popinfos.push_back(new WorkerThread::PopInfo((*name).first)); 
  }
}

void WorkerThread::ProvinceGroup::addEu3Province (Object* prov) {
  if (find(eu3Provs.begin(), eu3Provs.end(), prov) != eu3Provs.end()) return;
  eu3Provs.push_back(prov);
}

void WorkerThread::ProvinceGroup::addVicProvince (Object* vicprov, Object* eu3prov) {
  if (find(vicProvs.begin(), vicProvs.end(), vicprov) != vicProvs.end()) return; 
  vicProvs.push_back(vicprov);
  bool farmers = true;
  if (0 == vicprov->getValue("farmers").size()) farmers = false;
  vicprov->setLeaf("canHaveFarmers", farmers ? "yes" : "no"); 
  objvec remPops;
  for (vector<PopInfo*>::iterator pinfo = popinfos.begin(); pinfo != popinfos.end(); ++pinfo) {
    double histFraction = boss->histPopFraction;
    if (((*pinfo)->ptype == "clerks") || ((*pinfo)->ptype == "craftsmen")) histFraction = 0; 
    
    objvec pops = vicprov->getValue((*pinfo)->ptype);
    for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
      string dummy(""); 
      boss->resetCulture((*pop), eu3prov, dummy, true); 
      if (!boss->redistPops()) continue;
      double currSize = (*pop)->safeGetFloat("size");
      (*pinfo)->integratedLiteracy += currSize * (*pop)->safeGetFloat("literacy");
      if (currSize > poptypeToMaxSizeMap[(*pinfo)->ptype]) poptypeToMaxSizeMap[(*pinfo)->ptype] = currSize; 
      Object* issues = (*pop)->safeGetObject("issues");
      if (issues) {
	objvec leaves = issues->getLeaves();
	bool wantToUse = true;
	if (0 == leaves.size()) wantToUse = false;
	else {
	  for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
	    if (((*pinfo)->ptype == "slaves" ? 60 : 40) > atof((*l)->getLeaf().c_str())) continue;
	    wantToUse = false;
	    Logger::logStream(Logger::Debug) << "Threw out issue "
					     << (*l)->getKey()
					     << " with size "
					     << (*l)->getLeaf()
					     << " for "
					     << (*pinfo)->ptype
					     << "\n"; 
	    break; 
	  }
	}
	
	if (wantToUse) {
	  issues->resetLeaf("size", currSize);
	  poptypeToIssuesMap[(*pinfo)->ptype].push_back(issues);
	}
      }
      Object* ideology = (*pop)->safeGetObject("ideology");
      if ((ideology) && (0 < ideology->getLeaves().size())) {ideology->resetLeaf("size", currSize); poptypeToIdeologyMap[(*pinfo)->ptype].push_back(ideology);}
      if (currSize*histFraction < 100) {
	(*pinfo)->worldTotal += currSize;
	remPops.push_back(*pop); 
      }
      else {
	(*pinfo)->worldTotal += (1 - histFraction)*currSize;
	(*pop)->resetLeaf("size", (int) floor(currSize*histFraction + 0.5));
	(*pop)->unsetValue("literacy");
	(*pop)->unsetValue("issues");
	(*pop)->unsetValue("ideology"); 
      }
    }
    if ((boss->redistPops()) && (histFraction < 0.01)) vicprov->unsetValue((*pinfo)->ptype);
  }
  for (objiter rm = remPops.begin(); rm != remPops.end(); ++rm) {
    vicprov->removeObject(*rm); 
  }
}

void WorkerThread::resetCulture (Object* pop, Object* eu3prov, string& minority, bool needsClear) {
  if (needsClear) {
    for (map<string, string>::iterator i = eu3CultureToVicCultureMap.begin(); i != eu3CultureToVicCultureMap.end(); ++i) {
      pop->unsetValue((*i).second); 
    }
  }

  string provCulture  = eu3prov->safeGetString("culture", "norwegian");
  if ((pop->getKey() == "farmers") || (pop->getKey() == "labourers")) {
    // For every culture-change in this province, check whether it was overseas at
    // the time. If so, ignore that culture change, unless the city size was zero at the time
    // - in this case, provide a minority. 

    bool debugProvince = eu3prov->safeGetString("debugPop", "no") == "yes";
    
    Object* history = eu3prov->safeGetObject("history");
    if (!history) goto neverMind; 

    provCulture = history->safeGetString("culture", provCulture); 
    double citysize = history->safeGetFloat("citysize"); 
    
    if (debugProvince) Logger::logStream(PopDebug) << "Initial culture of " << nameAndNumber(eu3prov) << " : " << provCulture << "\n";

    string currCulture = provCulture; 
    for (vector<ProvinceHistory*>::iterator i = eu3ProvinceToHistoryMap[eu3prov].begin(); i != eu3ProvinceToHistoryMap[eu3prov].end(); ++i) {
      if ((*i)->citysize > 0) citysize = (*i)->citysize; 
      if ((*i)->culture == currCulture) continue;
      currCulture = (*i)->culture; 
      if (isOverseasEu3Provinces((*i)->capital, eu3prov)) {
	if (citysize > 0) continue;
	else minority = provCulture; 
      }
      provCulture = currCulture; 
      
      if (debugProvince) {
	Logger::logStream(PopDebug) << "  Culture change to "
				    << (*i)->culture << " "
				    << (*i)->date << " "; 
	if (((*i)->capital) && (eu3ProvinceToVicProvincesMap[(*i)->capital].size() > 0)) {
	  Logger::logStream(PopDebug) << (*i)->capital->getKey() << " "
				      << eu3ProvinceToVicProvincesMap[(*i)->capital][0]->safeGetString("continent", "nocon")
				      << " "; 
	}
	else Logger::logStream(PopDebug) << "bad cap ";
	if (eu3ProvinceToVicProvincesMap[eu3prov].size() > 0) {
	  Logger::logStream(PopDebug) << eu3ProvinceToVicProvincesMap[eu3prov][0]->safeGetString("continent", "nocon");
	}
	Logger::logStream(PopDebug) << "\n";
      }
    }
  }

 neverMind: 
  string provReligion = eu3prov->safeGetString("religion", "protestant");
  pop->setLeaf(eu3CultureToVicCulture(provCulture), 
	       eu3CultureToVicCulture(provReligion)); 
}

double WorkerThread::calculateLiteracy (string poptype, Object* eu3prov) {
  double ret = 1; 
  Object* literacy = poptypes[poptype]->safeGetObject("literacy"); 
  Object* owner = findEu3CountryByTag(eu3prov->safeGetString("owner","NONE"));
  //bool debug = eu3prov->safeGetString("debugLiteracy", "no") == "yes";
  if (owner) {
    for (vector<string>::iterator sl = sliders.begin(); sl != sliders.end(); ++sl) {
      ret += literacy->safeGetFloat(*sl)*owner->safeGetInt(*sl, 0);
    }

    //if ((debug) && (eu3prov->safeGetString(poptype + "printed", "no") == "no"))
    //Logger::logStream(Logger::Debug) << "Province "
    //				       << eu3prov->getKey()
    //				       << " " << poptype 
    //				       << " literacy from sliders "
    //				       << ret;
    double ideas = 0; 
    for (map<string, Object*>::iterator idea = ideasMap.begin(); idea != ideasMap.end(); ++idea) {
      if (owner->safeGetString((*idea).first, "no") != "yes") continue;
      ideas += literacy->safeGetFloat((*idea).first);
    }
    //if ((debug) && (eu3prov->safeGetString(poptype + "printed", "no") == "no")) {
    //Logger::logStream(Logger::Debug) << ", from ideas "
    //				       << ideas
    //				       << ".\n";
    //eu3prov->resetLeaf(poptype + "printed", "yes"); 
    //}
    ret += ideas; 
  }

  return ret; 
}

void WorkerThread::ProvinceGroup::moveLiteracy () {
  for (vector<PopInfo*>::iterator pinfo = popinfos.begin(); pinfo != popinfos.end(); ++pinfo) {
    objvec popsForLiteracyRedist;
    double totalLiteracyWeight = 0;
    for (objiter vprov = vicProvs.begin(); vprov != vicProvs.end(); ++vprov) {
      objvec pops = (*vprov)->getValue((*pinfo)->ptype); 
      Object* eu3prov = boss->vicProvinceToEu3ProvincesMap[*vprov][0];
      
      for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
	if (eu3prov->safeGetString("debugLiteracy", "no") == "yes") {
	  (*pop)->resetLeaf("debugLiteracy", "yes");
	  (*pop)->resetLeaf("province", eu3prov->getKey()); 
	}
	
	double uniWeight = eu3prov->safeGetFloat("literacyInfluence") * boss->calculateLiteracy((*pinfo)->ptype, eu3prov) * (*pop)->safeGetFloat("size"); 
	if ((*pop)->safeGetString("native", "no") == "yes") uniWeight *= 0.5; 
	(*pop)->setLeaf("uniWeight", uniWeight);
	totalLiteracyWeight += uniWeight;
	popsForLiteracyRedist.push_back(*pop); 
      }
    }
    
    //bool printedLiteracy = false; 
    for (objiter pop = popsForLiteracyRedist.begin(); pop != popsForLiteracyRedist.end(); ++pop) {
      double lit = (*pinfo)->integratedLiteracy * ((*pop)->safeGetFloat("uniWeight") / totalLiteracyWeight) / (*pop)->safeGetFloat("size");
      (*pop)->resetLeaf("literacy", min(1.0, lit)); 
      (*pop)->unsetValue("uniWeight");
      (*pop)->unsetValue("debugLiteracy");
      (*pop)->unsetValue("province");
      (*pop)->unsetValue("native"); 
    }
  }
}


void WorkerThread::ProvinceGroup::redistribute (int& popid) {
  map<string, pair<double, int> > sizeMap; 
  
  for (vector<PopInfo*>::iterator poptype = popinfos.begin(); poptype != popinfos.end(); ++poptype) {
    double totalWeight = 0;
    for (objiter vprov = vicProvs.begin(); vprov != vicProvs.end(); ++vprov) {     
      if ((((*poptype)->ptype == "craftsmen") || ((*poptype)->ptype == "clerks")) && ((*vprov)->safeGetString("factory", "no") != "yes")) continue; 
      for (objiter eu3prov = boss->vicProvinceToEu3ProvincesMap[*vprov].begin(); eu3prov != boss->vicProvinceToEu3ProvincesMap[*vprov].end(); ++eu3prov) {
	totalWeight += boss->getPopWeight(*eu3prov, (*poptype)->ptype);
      }
    }
    
    Logger::logStream(PopDebug) << "Total weight of "
				<< (*poptype)->ptype << " "
				<< totalWeight << " "
				<< "\n"; 


    double overflow = 0;
    for (objiter vprov = vicProvs.begin(); vprov != vicProvs.end(); ++vprov) {     
      double weight = 0;

      if ((((*poptype)->ptype == "craftsmen") || ((*poptype)->ptype == "clerks")) && ((*vprov)->safeGetString("factory", "no") != "yes")) continue; 
      for (objiter eu3prov = boss->vicProvinceToEu3ProvincesMap[*vprov].begin(); eu3prov != boss->vicProvinceToEu3ProvincesMap[*vprov].end(); ++eu3prov) {
	double pweight = boss->getPopWeight(*eu3prov, (*poptype)->ptype);
	sizeMap[(*vprov)->safeGetString("owner", "NONE")].first  += pweight; 
	pweight /= totalWeight;
	pweight *= (*poptype)->worldTotal;
	weight += pweight;

	Object* sources = (*eu3prov)->safeGetObject("popWeights");
	if (!sources) continue;
	Object* eu3Owner = boss->findEu3CountryByTag((*eu3prov)->safeGetString("owner"));
	if (!eu3Owner) continue;

	PopSourceInfo* sinfo = boss->eu3CountryToPopSourceMap[eu3Owner];
	if (!sinfo) {
	  sinfo = new PopSourceInfo();
	  sinfo->tag = eu3Owner->getKey(); 
	  boss->eu3CountryToPopSourceMap[eu3Owner] = sinfo; 
	}

	sinfo->manpower[(*eu3prov)->safeGetString("trade_goods")] += sources->safeGetFloat("manpower");
	sinfo->goods[(*eu3prov)->safeGetString("trade_goods")] += sources->safeGetFloat("production");
	sinfo->occupation.first  += sources->safeGetFloat("beforeOccupation");
	sinfo->occupation.second += sources->safeGetFloat("beforeReligion");
	sinfo->religion.first    += sources->safeGetFloat("beforeReligion");
	sinfo->religion.second   += sources->safeGetFloat("beforeBuildings");
	sinfo->buildings.first   += sources->safeGetFloat("beforeBuildings");
	sinfo->buildings.second  += sources->safeGetFloat("overall");
	sinfo->final             += sources->safeGetFloat("overall");
	
      }
      double size = floor(weight + overflow + 0.5);
      if (size < 250) {
	overflow += weight;
	continue; 
      }

      if ((((*poptype)->ptype == "farmers") || ((*poptype)->ptype == "labourers")) && (boss->configObject->safeGetString("hugePops", "no") == "yes")) size = 4000000; 
      
      Object* pop = new Object((*poptype)->ptype);
      pop->setLeaf("id", popid++);      
      pop->setLeaf("size", (int) size);
      if (0 > pop->safeGetInt("size", 0)) {
	static bool tested = false;
	if (!tested) {
	  Logger::logStream(Logger::Error) << "Error: Pop has negative size. "
					   << size << " "
					   << (int) size << " "
					   << overflow << " "
					   << weight << " "
					   << totalWeight << " "
					   << (*poptype)->worldTotal << " "
					   << (*poptype)->ptype << " "
					   << "\n";
  
	  tested = true;
	  fullPopWeightDebug = true;
	  for (objiter eu3prov = boss->vicProvinceToEu3ProvincesMap[*vprov].begin(); eu3prov != boss->vicProvinceToEu3ProvincesMap[*vprov].end(); ++eu3prov) {
	    boss->getPopWeight(*eu3prov, (*poptype)->ptype);
	  }
	  fullPopWeightDebug = false; 
	}
      }
      overflow = 0;
      while (poptypeToIssuesMap[(*poptype)->ptype].size() > 0) {
	int issue = poptypeToIssuesMap[(*poptype)->ptype].size() - 1;
	double curr = rand(); 
	issue = floor((curr / RAND_MAX) * issue);
	curr = rand();
	curr /= RAND_MAX;	
	curr *= poptypeToMaxSizeMap[(*poptype)->ptype];
	Object* candidate = poptypeToIssuesMap[(*poptype)->ptype][issue];
	if (curr > candidate->safeGetFloat("size")) continue;
	pop->setValue(candidate);
	break; 
      }

      while (poptypeToIdeologyMap[(*poptype)->ptype].size() > 0) {
	int issue = poptypeToIdeologyMap[(*poptype)->ptype].size() - 1;
	double curr = rand(); 
	issue = floor((curr / RAND_MAX) * issue);
	curr = rand();
	curr /= RAND_MAX;
	curr *= poptypeToMaxSizeMap[(*poptype)->ptype];	
	if (curr > poptypeToIdeologyMap[(*poptype)->ptype][issue]->safeGetFloat("size")) continue; 
	pop->setValue(poptypeToIdeologyMap[(*poptype)->ptype][issue]);
	break; 
      }

      Object* eu3prov = boss->vicProvinceToEu3ProvincesMap[*vprov][0];
      string minority(""); 
      boss->resetCulture(pop, eu3prov, minority);
      (*vprov)->setValue(pop);
      sizeMap[(*vprov)->safeGetString("owner", "NONE")].second += size;
      if (minority != "") {
	Object* natives = new Object((*poptype)->ptype);
	natives->setLeaf("id", popid++);
	natives->setLeaf("size", (int) floor(size * 0.1 + 0.5));
	pop->resetLeaf("size", (int) floor(size * 0.9 + 0.5));
	natives->setLeaf("native", "yes"); 
	string rel = eu3prov->safeGetString("religion"); 
	natives->setLeaf(boss->eu3CultureToVicCulture(minority), boss->eu3CultureToVicCulture(rel));
	(*vprov)->setValue(natives); 
      }
    }
  }

  for (map<string, pair<double, int> >::iterator i = sizeMap.begin(); i != sizeMap.end(); ++i) {
    Logger::logStream(WorkerThread::PopDebug) << (*i).first << " : " << (*i).second.first << " " << (4 * (*i).second.second) << "\n"; 
  }
}

void WorkerThread::nationalValues () {
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* eu3 = findEu3CountryByVicCountry(*vic);
    if (!eu3) continue;
    double bestScore = -10000000;
    objiter bestValue = natValues.begin();
    for (objiter v = natValues.begin(); v != natValues.end(); ++v) {
      double points = 0.01;
      objvec leaves = (*v)->getLeaves();
      for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
	if (eu3->safeGetString((*leaf)->getKey(), "no") == "yes") points += (*v)->safeGetFloat((*leaf)->getKey());
	else if (eu3->safeGetFloat((*leaf)->getKey(), 0) != 0) points += (*v)->safeGetFloat((*leaf)->getKey()) * eu3->safeGetFloat((*leaf)->getKey(), 0);
      }

      (*vic)->resetLeaf(remQuotes((*v)->safeGetString("name")), points); 
      if (points < bestScore) continue;

      bestScore = points;
      bestValue = v; 
    }
    (*vic)->resetLeaf("nationalvalue", (*bestValue)->safeGetString("name")); 
  }
}

string WorkerThread::eu3CultureToVicCulture (string cult) {
  string ret = eu3CultureToVicCultureMap[cult];
  if (ret == "") {
    ret = eu3CultureToVicCultureMap[remQuotes(cult)];
    if (ret == "") {
      Logger::logStream(Logger::Error) << "Error: Could not find conversion for EU3 culture " << cult << "\n";
      return "norwegian";
    } 
  }
  return ret; 
}

void WorkerThread::ProvinceGroup::addToGroup (string s) {
  allGroups[s] = this; 
}

struct SizeInfo {
  SizeInfo (); 
  double chisq ();
  double predicted (); 
  static double iterate (vector<double>& minima, vector<double>& maxima, vector<SizeInfo*>& infos, vector<double>& bestValues); 

    int size;
  vector<int> numbers;
  static vector<double> weights; 
  static double step; 
};

//double SizeInfo::step = 0.33333;
//double SizeInfo::step = 0.49999;
double SizeInfo::step = 0.2499999; 

vector<double> SizeInfo::weights; 

SizeInfo::SizeInfo ()
  : size(0)
{
  numbers.resize(11);
  if (0 == weights.size()) weights.resize(12); 
}

double SizeInfo::predicted () {
  double ret = 0;
  for (int i = 0; i < 11; ++i) {
    ret += weights[i] * numbers[i]; 
  }
  ret *= weights[11]*0.05;
  return ret; 
}

double SizeInfo::chisq () {
  double ret = predicted(); 
  ret -= size;
  return ret*ret; 
}

double SizeInfo::iterate (vector<double>& minima, vector<double>& maxima, vector<SizeInfo*>& infos, vector<double>& bestValues) {
  bool ticked[12];
  for (int i = 0; i < 12; ++i) {
    SizeInfo::weights[i] = minima[i];
    ticked[i] = false;
  }

  double bestChisq = 0;
  for (vector<SizeInfo*>::iterator s = infos.begin(); s != infos.end(); ++s) {
    bestChisq += (*s)->chisq(); 
  }
  
  while (SizeInfo::weights[0] <= maxima[0]) {
    int ticker = 11;
    while (ticker >= 0) {
      SizeInfo::weights[ticker] += step*(maxima[ticker] - minima[ticker]);
      if (SizeInfo::weights[ticker] <= maxima[ticker]) break;
      if (ticker > 0) SizeInfo::weights[ticker] = minima[ticker];
      if (!ticked[ticker]) {
	ticked[ticker] = true;
	Logger::logStream(Logger::Game) << "Reached " << ticker << "\n"; 
      }
      ticker--;
    }
    if (0 == ticker) {
      Logger::logStream(Logger::Game) << "Turnover " << SizeInfo::weights[0] << "\n";
      for (int i = 0; i < 12; ++i) ticked[i] = false; 
    }

    double currChisq = 0;
    for (vector<SizeInfo*>::iterator s = infos.begin(); s != infos.end(); ++s) {
      currChisq += (*s)->chisq();
      if (currChisq >= bestChisq) break; 
    }
    if (currChisq >= bestChisq) continue;
    bestChisq = currChisq;
    Logger::logStream(Logger::Game) << "New best chi-square : "
				    << bestChisq;
    for (int j = 0; j < 12; ++j) {
      bestValues[j] = SizeInfo::weights[j];
      Logger::logStream(Logger::Game) << " " << bestValues[j];
    }
    Logger::logStream(Logger::Game) << "\n"; 
  }
  return bestChisq; 
}

void WorkerThread::messWithSize () {
  vicGame = loadTextFile(targetVersion + "input.v2");
  objvec vicObjects = vicGame->getLeaves(); 
  for (objiter vp = vicObjects.begin(); vp != vicObjects.end(); ++vp) {
    // RGO must be defined after POPs
    Object* rgo = (*vp)->safeGetObject("rgo");
    if (!rgo) continue;

    string historyFile = remQuotes((*vp)->safeGetString("name")); 
    Object* original = loadTextFile(targetVersion + "history\\provinces\\" + historyFile + ".txt");
    original = original; 
  }

  
  /*
  
  Object* sizeInfo = loadTextFile(targetVersion + "v2terrain.txt");
  if (!sizeInfo) {
    Logger::logStream(Logger::Error) << "Couldn't find v2terrain.txt.\n";
    return; 
  }
  Object* posInfo = loadTextFile(targetVersion + "v2positions.txt"); 
  if (!posInfo) {
    Logger::logStream(Logger::Error) << "Couldn't find v2positions.txt.\n";
    return; 
  }
  map<string, Object*> posMap; 
  objvec poses = posInfo->getLeaves();
  for (objiter p = poses.begin(); p != poses.end(); ++p) {
    posMap[(*p)->getKey()] = (*p); 
  }
  
  
  vicGame = loadTextFile(targetVersion + "input.v2");
  objvec provs = sizeInfo->getLeaves(); 
  for (objiter p = provs.begin(); p != provs.end(); ++p) {
    Object* gameProv = vicGame->safeGetObject((*p)->getKey());
    if (!gameProv) continue;
    Object* rgo = gameProv->safeGetObject("rgo");
    if (0 == rgo) continue;
    int size = 0;
    objvec labs = gameProv->getValue("labourers");
    for (objiter l = labs.begin(); l != labs.end(); ++l) {
      size += (*l)->safeGetInt("size");
    }
    objvec farms = gameProv->getValue("farmers");
    for (objiter l = farms.begin(); l != farms.end(); ++l) {
      size += (*l)->safeGetInt("size");
    }

    (*p)->resetLeaf("population", size);
    (*p)->resetLeaf("life_rating", gameProv->safeGetString("life_rating", "35"));
    (*p)->resetLeaf("product", rgo->safeGetString("goods_type", "nothing"));

    Object* unitpos = posMap[gameProv->getKey()]->safeGetObject("unit");
    if (!unitpos) unitpos = posMap[gameProv->getKey()]->safeGetObject("text_position");
    if (!unitpos) unitpos = posMap[gameProv->getKey()]->safeGetObject("factory");
    if (unitpos) (*p)->resetLeaf("latitude", unitpos->safeGetString("y"));
    else {
      (*p)->resetLeaf("latitude", "1080");
      Logger::logStream(Logger::Game) << "Could not find latitude for "
				      << gameProv->getKey()
				      << "\n"; 
    }
  }
  ofstream writer;
  writer.open("newV2Terrain.txt");
  Parser::topLevel = sizeInfo; 
  writer << (*sizeInfo);
  writer.close();

  Logger::logStream(Logger::Game) << "Wrote file newV2Terrain.txt, done.\n";
  */
}

void WorkerThread::configure () {
  configObject = loadTextFile("config.txt");
  srand(configObject->safeGetInt("randseed")); 
  targetVersion = configObject->safeGetString("moddir", ".\\DW\\");
  customObject = loadTextFile(targetVersion + "Custom.txt");
  
  if (targetVersion == ".\\AHD\\") {
    Object* provdirlist = configObject->safeGetObject("provdirs");
    provdirs = provdirlist->getValue("dir");
    vicTechs = loadTextFile(targetVersion + "victechs.txt");
    assert(vicTechs);
    assert(vicTechs->getLeaves().size());
  }
  
  Logger::logStream(Logger::Debug).setActive(false);
  Logger::logStream(PopDebug).setActive(false);
  if (configObject->safeGetString("debug", "no") == "yes") Logger::logStream(Logger::Debug).setActive(true);
  if (configObject->safeGetString("popdebug", "no") == "yes") Logger::logStream(PopDebug).setActive(true);
  poptypes.clear();
  Object* popTypeObj = configObject->safeGetObject("poptypes");
  objvec pops = popTypeObj->getLeaves();
  for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
    poptypes[(*pop)->getKey()] = (*pop); 
  }
  
  objvec groups = configObject->getValue("provgroup");
  for (objiter gr = groups.begin(); gr != groups.end(); ++gr) {
    ProvinceGroup* group = new ProvinceGroup();
    provinceGroups.push_back(group); 
    objvec goods = (*gr)->getValue("trade_goods");
    for (objiter good = goods.begin(); good != goods.end(); ++good) {
      group->addToGroup((*good)->getLeaf()); 
    }
  }

  traitObject = loadTextFile(targetVersion + "traits.txt");
  assert(traitObject);
  assert(traitObject->safeGetObject("personality"));
  assert(traitObject->safeGetObject("background")); 
  
  Object* valuesObject = loadTextFile(targetVersion + "natvalues.txt");
  natValues = valuesObject->getValue("value"); 
  
  Object* tmods = loadTextFile(targetVersion + "triggered_modifiers.txt");
  objvec tm = tmods->getLeaves();
  for (objiter t = tm.begin(); t != tm.end(); ++t) {
    trigMods[(*t)->getKey()] = (*t);
  } 
  
  Object* decisions = loadTextFile(targetVersion + "decisions.txt");
  objvec decs = decisions->getLeaves();
  for (objiter dec = decs.begin(); dec != decs.end(); ++dec) {
    nameToDecisionMap[(*dec)->getKey()] = (*dec); 
  }

  Object* modifiers = loadTextFile(targetVersion + "event_modifiers.txt");
  objvec mods = modifiers->getLeaves();
  for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
    nameToModifierMap[(*mod)->getKey()] = (*mod); 
  }

  eu3ContinentObject = loadTextFile(targetVersion + "eu3Continents.txt");
  
  Object* buildings = loadTextFile(targetVersion + "buildings.txt");
  buildingTypes = buildings->getLeaves();
  //for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
  //Logger::logStream(Logger::Debug) << (*b) << "\n"; 
  //}

  Object* technology = loadTextFile(targetVersion + "techs.txt");
  techs = technology->getLeaves();

  productionEfficiencies.resize(100); 
  for (objiter build = buildingTypes.begin(); build != buildingTypes.end(); ++build) {
    for (objiter group = techs.begin(); group != techs.end(); ++group) {
      objvec specs = (*group)->getValue("technology");
      for (objiter tech = specs.begin(); tech != specs.end(); ++tech) {
	string date = (*tech)->safeGetString("average_year", "1399");
	date += ".1.1";
	(*tech)->setLeaf("days", days(date));
	if ((*group)->getKey() == "production_tech") productionEfficiencies[(*tech)->safeGetInt("id")] = (*tech)->safeGetFloat("production_efficiency"); 
	if ((*tech)->safeGetString((*build)->getKey(), "no") != "yes") continue;
	(*build)->resetLeaf("date", date);
      } 
    }
  }

  histPopFraction = configObject->safeGetFloat("histPopFraction", 0.5);

  Object* cults = loadTextFile(targetVersion + "cultureMap.txt");
  objvec links = cults->getValue("link");
  for (objiter l = links.begin(); l != links.end(); ++l) {
    objvec sources = (*l)->getValue("eu3");
    string target = (*l)->safeGetString("vic"); 
    for (objiter source = sources.begin(); source != sources.end(); ++source) {
      eu3CultureToVicCultureMap[(*source)->getLeaf()] = target; 
    }
  }

  Object* regs = loadTextFile(targetVersion + "region.txt");
  regions = regs->getLeaves();

  Object* contObject = loadTextFile(targetVersion + "continent.txt");
  continents = contObject->getLeaves(); 

  if (configObject->safeGetString("getPopWeights", "calculate") == "readFromCache") {
    cachedPopWeights = loadTextFile("./priceCache/popWeights.txt"); 
  }

  Object* histPrices = loadTextFile(targetVersion + "historicalPrices.txt");
  historicalPrices = histPrices->getLeaves();
  static DateAscendingSorter dateSorter;
  sort(historicalPrices.begin(), historicalPrices.end(), dateSorter); 
  
  
  // Load vanilla parties 
  countryPointers = loadTextFile(targetVersion + "countries.txt");
  objvec taglist = countryPointers->getLeaves();

  for (objiter tag = taglist.begin(); tag != taglist.end(); ++tag) {
    string currPartyFile = targetVersion + remQuotes((*tag)->getLeaf());
    tagToPartiesMap[(*tag)->getKey()] = loadTextFile(currPartyFile); 
  }

  Object* pinfoObj = loadTextFile(targetVersion + "positions.txt");
  objvec pinfos = pinfoObj->getLeaves();
  for (objiter pinfo = pinfos.begin(); pinfo != pinfos.end(); ++pinfo) {
    eu3ProvNumToEu3ProvInfoMap[(*pinfo)->getKey()] = (*pinfo); 
  }

  pinfoObj = loadTextFile(targetVersion + "vicpositions.txt");
  pinfos = pinfoObj->getLeaves();
  for (objiter pinfo = pinfos.begin(); pinfo != pinfos.end(); ++pinfo) {
    vicProvNumToVicProvInfoMap[(*pinfo)->getKey()] = (*pinfo); 
  }

  Object* religionFile = loadTextFile(targetVersion + "religion.txt");
  objvec relGroups = religionFile->getLeaves();
  for (objiter group = relGroups.begin(); group != relGroups.end(); ++group) {
    objvec religions = (*group)->getLeaves();
    for (objiter rel = religions.begin(); rel != religions.end(); ++rel) {
      (*rel)->resetLeaf("group", (*group)->getKey());
      religionMap[(*rel)->getKey()] = (*rel); 
    }
  }
  
  Object* governmentFile = loadTextFile(targetVersion + "governments.txt");
  objvec govs = governmentFile->getLeaves();
  for (objiter gov = govs.begin(); gov != govs.end(); ++gov) {
    governmentMap[(*gov)->getKey()] = (*gov); 
  }

  Object* ideasFile = loadTextFile(targetVersion + "ideas.txt");
  objvec ideaGroups = ideasFile->getLeaves();
  for (objiter ideaGroup = ideaGroups.begin(); ideaGroup != ideaGroups.end(); ++ideaGroup) {
    objvec ideas = (*ideaGroup)->getLeaves(); 
    for (objiter idea = ideas.begin(); idea != ideas.end(); ++idea) {
      (*idea)->resetLeaf("group", (*ideaGroup)->getKey());
      ideasMap[(*idea)->getKey()] = (*idea); 
    }
  }

  Object* priceFile = loadTextFile(targetVersion + "Prices.txt");
  goods = priceFile->getLeaves();

  rgoList = loadTextFile(targetVersion + "redist.txt");
}

Object* WorkerThread::getEu3ProvInfoFromEu3Province (Object* eu3prov) {
  if (!eu3prov) {
    Logger::logStream(Logger::Error) << "Error: Attempt to get EU3 province information on null pointer.\n";
    return 0; 
  }
  Object* ret = eu3ProvNumToEu3ProvInfoMap[eu3prov->getKey()];
  if (!ret) {
    Logger::logStream(Logger::Error) << "Error: Could not find province info for EU3 province "
				     << nameAndNumber(eu3prov)
				     << ".\n";
  }
  return ret;
}

Object* WorkerThread::getVicProvInfoFromVicProvince (Object* vicprov) {
  if (!vicprov) {
    Logger::logStream(Logger::Error) << "Error: Attempt to get Vic province information on null pointer.\n";
    return 0; 
  }
  Object* ret = vicProvNumToVicProvInfoMap[vicprov->getKey()];
  if (!ret) {
    Logger::logStream(Logger::Error) << "Error: Could not find province info for Vic province "
				     << nameAndNumber(vicprov)
				     << ".\n";
  }
  return ret;
}

void WorkerThread::clearVicOwners () {
  objvec allVicObjects = vicGame->getLeaves();
  for (objiter g = allVicObjects.begin(); g != allVicObjects.end(); ++g) {
    (*g)->unsetValue("owner");
    (*g)->unsetValue("core");
    (*g)->unsetValue("controller");
    (*g)->unsetValue("crime");
    (*g)->unsetValue("creditor");
    (*g)->unsetValue("railroad");
    (*g)->unsetValue("military_construction");
    (*g)->unsetValue("building_construction");    
    objvec states = (*g)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec facs = (*state)->getValue("state_buildings");
      for (objiter fac = facs.begin(); fac != facs.end(); ++fac) {
	(*fac)->unsetValue("employment");
	(*fac)->resetLeaf("money", "1000.000");
	(*fac)->unsetValue("last_spending");
	(*fac)->unsetValue("last_income"); 
	vicFactories.push_back(*fac);
      }
    }
    (*g)->unsetValue("state");
    (*g)->unsetValue("research");

    if ((*g)->safeGetString("wage_reform",            "NOREFORM") != "NOREFORM") (*g)->resetLeaf("wage_reform",            "no_minimum_wage");
    if ((*g)->safeGetString("work_hours",             "NOREFORM") != "NOREFORM") (*g)->resetLeaf("work_hours",             "no_work_hour_limit");
    if ((*g)->safeGetString("safety_regulations",     "NOREFORM") != "NOREFORM") (*g)->resetLeaf("safety_regulations",     "no_safety");
    if ((*g)->safeGetString("unemployment_subsidies", "NOREFORM") != "NOREFORM") (*g)->resetLeaf("unemployment_subsidies", "no_subsidies");
    if ((*g)->safeGetString("pensions",               "NOREFORM") != "NOREFORM") (*g)->resetLeaf("pensions",               "no_pensions");
    if ((*g)->safeGetString("health_care",            "NOREFORM") != "NOREFORM") (*g)->resetLeaf("health_care",            "no_health_care");
  }
}

void WorkerThread::createProvinceMappings () {
  Logger::logStream(Logger::Game) << "Loading province mappings.\n";
  Object* provmapping = loadTextFile(targetVersion + "province_mappings.txt");
  objvec links = provmapping->getValue("link");
  map<string, bool> gotProvinces;
  
  objvec allEu3Objects = eu3Game->getLeaves();
  for (objiter g = allEu3Objects.begin(); g != allEu3Objects.end(); ++g) {
    if (0 == atoi((*g)->getKey().c_str())) continue;
    if ((*g)->safeGetString("owner", "NONE") == "NONE") continue; 
    gotProvinces[(*g)->getKey()] = false; 
  }
  objvec doubleMapped; 

  for (objiter link = links.begin(); link != links.end(); ++link) {
    string eu3provnum = (*link)->safeGetString("eu3");
    Object* eu3prov = eu3Game->safeGetObject(eu3provnum);
    assert(eu3prov);
    objvec vics = (*link)->getValue("vic");
    for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
      string vicprovnum = (*vic)->getLeaf();
      Object* vicprov = vicGame->safeGetObject(vicprovnum);
      assert(vicprov);
      string vicContinent = "none"; 
      for (objiter cont = continents.begin(); cont != continents.end(); ++cont) {
	if ((*cont)->safeGetString(vicprovnum, "no") != "yes") continue;
	vicContinent = (*cont)->getKey();
	break;
      }
      if (vicContinent == "none") {
	Logger::logStream(Logger::Warning) << "Warning: Could not find continent for Vic province "
					   << nameAndNumber(vicprov)
					   << ", assuming Europe.\n";
	vicContinent = "europe";
      }
      vicprov->resetLeaf("continent", vicContinent);
      vicprov->resetLeaf("life_rating", "35"); 
      eu3prov->resetLeaf("continent", vicContinent);
      eu3ProvinceToVicProvincesMap[eu3prov].push_back(vicprov);
      vicProvinceToEu3ProvincesMap[vicprov].push_back(eu3prov);
    }
  }
  
  for (objiter link = links.begin(); link != links.end(); ++link) {
    objvec eu3nums = (*link)->getValue("eu3");
    for (unsigned int i = 1; i < eu3nums.size(); ++i) {
      doubleMapped.push_back(eu3Game->safeGetObject(eu3nums[i]->getLeaf()));
      gotProvinces[eu3nums[i]->getLeaf()] = true;
      Object* eu3prov = eu3Game->safeGetObject(eu3nums[i]->getLeaf());
      if (!eu3prov) continue;
      objvec vics = (*link)->getValue("vic");
      for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
	Object* vicprov = vicGame->safeGetObject((*vic)->getLeaf());
	if (!vicprov) continue;
	if (find(vicProvinceToEu3ProvincesMap[vicprov].begin(), vicProvinceToEu3ProvincesMap[vicprov].end(), eu3prov) == vicProvinceToEu3ProvincesMap[vicprov].end())
	  vicProvinceToEu3ProvincesMap[vicprov].push_back(eu3prov);
      }
    }

    string eu3provnum = (*link)->safeGetString("eu3");
    Object* eu3prov = eu3Game->safeGetObject(eu3provnum);
    assert(eu3prov);
    Logger::logStream(Logger::Debug) << "Mapping province "
				     << nameAndNumber(eu3prov) << " "
				     << eu3prov->safeGetString("trade_goods", "notradegood")
				     << "\n";
    gotProvinces[eu3prov->getKey()] = true; 
    ProvinceGroup* pgroup = ProvinceGroup::getGroup(eu3prov->safeGetString("trade_goods", "grain"));
    objvec vics = (*link)->getValue("vic");
    for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
      string vicprovnum = (*vic)->getLeaf();
      Object* vicprov = vicGame->safeGetObject(vicprovnum);
      assert(vicprov);
      vicProvinces.push_back(vicprov);
      if (pgroup) pgroup->addVicProvince(vicprov, eu3prov); 
      vicProvIdToVicProvinceMap[vicprovnum] = vicprov; 
      for (unsigned int i = 1; i < eu3nums.size(); ++i) {
	Object* dprov = eu3Game->safeGetObject(eu3nums[i]->getLeaf());
	if (!dprov) continue;
	dprov->resetLeaf("continent", vicprov->safeGetString("continent", "europe")); 
	eu3ProvinceToVicProvincesMap[dprov].push_back(vicprov);
      }
    }
    if (pgroup) pgroup->addEu3Province(eu3prov);
  }

  for (map<string, bool>::iterator i = gotProvinces.begin(); i != gotProvinces.end(); ++i) {
    if ((*i).second) continue;
    Logger::logStream(Logger::Debug) << "Could not find map for EU3 province "
				     << nameAndNumber(eu3Game->safeGetObject((*i).first)) 
				     << ".\n"; 
  }
  for (objiter dm = doubleMapped.begin(); dm != doubleMapped.end(); ++dm) {
    Logger::logStream(Logger::Debug) << "Double link: "
				     << nameAndNumber(*dm)
				     << "\n"; 
  }
  
  Logger::logStream(Logger::Game) << "Done with province mappings.\n";
}

void WorkerThread::createCountryMappings () {
  Logger::logStream(Logger::Game) << "Loading country mappings.\n";
  Object* provmapping = loadTextFile(targetVersion + "country_mappings.txt");
  objvec links = provmapping->getValue("link");
  map<Object*, objvec> tempMap; 
  for (objiter link = links.begin(); link != links.end(); ++link) {
    objvec eu3s = (*link)->getValue("eu3");
    objvec vics = (*link)->getValue("vic");
    for (objiter eu3 = eu3s.begin(); eu3 != eu3s.end(); ++eu3) {
      string eu3countrytag = (*eu3)->getLeaf(); 
      Object* eu3country = eu3Game->safeGetObject(eu3countrytag);
      if (!eu3country) {
	Logger::logStream(Logger::Debug) << "Warning: Could not find EU3 country "
					 << eu3countrytag
					 << " in save.\n"; 
	continue;
      }
      eu3Countries.push_back(eu3country);
      eu3TagToEu3CountryMap[eu3countrytag] = eu3country; 
      for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
	string viccountrytag = (*vic)->getLeaf();
	Object* viccountry = vicGame ? vicGame->safeGetObject(viccountrytag) : 0;
	if (!viccountry) {
	  if (vicGame) Logger::logStream(Logger::Debug) << "Warning: Could not find Vicky country \""
							<< viccountrytag
							<< "\" in input.\n"; 
	  continue;
	}
	if (find(vicCountries.begin(), vicCountries.end(), viccountry) == vicCountries.end()) vicCountries.push_back(viccountry);
	vicTagToVicCountryMap[viccountrytag] = viccountry;
	eu3CountryToVicCountryMap[eu3country] = viccountry;
	
	// Insert in sorted order 
	if ((0 == tempMap[viccountry].size()) ||
	    (eu3TagToSizeMap[tempMap[viccountry].back()->getKey()] > eu3TagToSizeMap[eu3country->getKey()])) {
	  tempMap[viccountry].push_back(eu3country);
	}
	else {
	  for (objiter c = tempMap[viccountry].begin(); c != tempMap[viccountry].end(); ++c) {
	    if (eu3TagToSizeMap[(*c)->getKey()] > eu3TagToSizeMap[eu3country->getKey()]) continue;
	    tempMap[viccountry].insert(c, eu3country);
	    break; 
	  }
	}
      }
    }
  }

  // Remap anything that changes sovereignty
  Object* diplomacy = eu3Game->safeGetObject("diplomacy", 0);
  if (!diplomacy) diplomacy = new Object("diplomacy");
  objvec vassals = diplomacy->getValue("vassal"); 
  for (map<Object*, objvec>::iterator m = tempMap.begin(); m != tempMap.end(); ++m) {
    Object* eu3main = (*m).second[0];
    vicCountryToEu3CountriesMap[(*m).first].push_back(eu3main);
    Logger::logStream(Logger::Debug) << "Vic tag " << (*m).first->getKey()
				     << " set to primary "
				     << eu3main->getKey() << "("
				     << eu3TagToSizeMap[eu3main->getKey()]
				     << ")\n"; 
    // None of the other countries should be vassals of any other power.
    // If they are, absorb them into their overlord.
    
    
    for (objiter eu3 = (*m).second.begin()+1; eu3 != (*m).second.end(); ++eu3) {
      for (objiter v = vassals.begin(); v != vassals.end(); ++v) {
	Object* currentVassal = findEu3CountryByTag((*v)->safeGetString("second", "\"NONE\""));
	if (currentVassal != (*eu3)) continue;
	Object* currentBoss = findEu3CountryByTag((*v)->safeGetString("first", "\"NONE\""));
	if (currentBoss == eu3main) continue;
	eu3CountryToVicCountryMap[currentVassal] = eu3CountryToVicCountryMap[currentBoss];
      }
    }
  }

  for (map<Object*, Object*>::iterator m = eu3CountryToVicCountryMap.begin(); m != eu3CountryToVicCountryMap.end(); ++m) {
    Object* viccountry = (*m).second;
    Object* eu3country = (*m).first; 
    if ((2 > vicCountryToEu3CountriesMap[viccountry].size()) ||
	(eu3TagToSizeMap[vicCountryToEu3CountriesMap[viccountry].back()->getKey()] > eu3TagToSizeMap[eu3country->getKey()])) {
      vicCountryToEu3CountriesMap[viccountry].push_back(eu3country);
    }
    else {
      objiter c = vicCountryToEu3CountriesMap[viccountry].begin(); 
      for (++c; c != vicCountryToEu3CountriesMap[viccountry].end(); ++c) {
	if (eu3TagToSizeMap[(*c)->getKey()] > eu3TagToSizeMap[eu3country->getKey()]) continue;
	vicCountryToEu3CountriesMap[viccountry].insert(c, eu3country);
	break; 
      }
    }
  }

  Object* govforms = configObject->safeGetObject("govforms");
  if (!govforms) govforms = new Object("govforms"); 

  if (vicGame) {
    Object* worldmarket = vicGame->safeGetObject("worldmarket");
    if (worldmarket) worldmarket = worldmarket->safeGetObject("worldmarket_pool");
    if (worldmarket) {
      objvec stocks = worldmarket->getLeaves();
      for (objiter stock = stocks.begin(); stock != stocks.end(); ++stock) {
	goodsToRedistribute[(*stock)->getKey()] += worldmarket->safeGetFloat((*stock)->getKey());
	goodsToRedistribute["total"] += worldmarket->safeGetFloat((*stock)->getKey());	
      }
    }
  }

    
  for (map<Object*, objvec>::iterator i = vicCountryToEu3CountriesMap.begin(); i != vicCountryToEu3CountriesMap.end(); ++i) {   
    Object* eu3Country = (*i).second[0];
    Object* viccountry = (*i).first;

    Object* stockpile = viccountry->safeGetObject("stockpile");
    if (stockpile) {
      objvec stocks = stockpile->getLeaves();
      for (objiter stock = stocks.begin(); stock != stocks.end(); ++stock) {
	goodsToRedistribute[(*stock)->getKey()] += stockpile->safeGetFloat((*stock)->getKey());
	goodsToRedistribute["total"] += stockpile->safeGetFloat((*stock)->getKey());	
      }
      stockpile->clear();
    }

    
    viccountry->resetLeaf("prestige", 50 + 0.5*eu3Country->safeGetFloat("precise_prestige", 0));
    Logger::logStream(Logger::Debug) << "Tag "
				     << viccountry->getKey()
				     << " has prestige "
				     << viccountry->safeGetString("prestige")
				     << " from tag "
				     << eu3Country->getKey()
				     << " with "
				     << eu3Country->safeGetString("precise_prestige", "0.000")
				     << ".\n"; 

    
    string primary = addQuotes(eu3CultureToVicCulture(eu3Country->safeGetString("primary_culture", "norwegian"))); 
    viccountry->setLeaf("primary_culture", primary);
    Logger::logStream(Logger::Debug) << "Culture conversion for tag " << viccountry->getKey() << " ("
				     << (*i).second[0]->getKey() << ") : "
				     << eu3CultureToVicCulture((*i).second[0]->safeGetString("primary_culture", "norwegian")) << "->"
				     << primary << " ";
    viccountry->setLeaf("religion", addQuotes(eu3CultureToVicCulture((*i).second[0]->safeGetString("religion", "protestant")))); 
    objvec accepted = (*i).second[0]->getValue("accepted_culture");
    vector<string> gotCultures;
    for (objiter acc = accepted.begin(); acc != accepted.end(); ++acc) {
      string curr = addQuotes(eu3CultureToVicCulture((*acc)->getLeaf()));
      if (curr == primary) continue;
      if (find(gotCultures.begin(), gotCultures.end(), curr) != gotCultures.end()) continue;
      gotCultures.push_back(curr);
      Logger::logStream(Logger::Debug) << (*acc)->getLeaf() << "->"
				       << curr << " ";
    }
    if (0 < gotCultures.size()) {
      Object* cultList = new Object("culture");
      cultList->setObjList();
      for (vector<string>::iterator c = gotCultures.begin(); c != gotCultures.end(); ++c) {
	cultList->addToList(*c);
      }
      viccountry->setValue(cultList);
    }
    Logger::logStream(Logger::Debug) << "\n";

    string currGov = remQuotes((*i).second[0]->safeGetString("government", "\"absolute_monarchy\""));
    viccountry->resetLeaf("government", govforms->safeGetString(currGov));
    Object* reforms = govforms->safeGetObject("reforms");
    assert(reforms);
    Object* currGovReforms = reforms->safeGetObject(currGov);
    if (currGovReforms) {
      objvec refs = currGovReforms->getLeaves();
      for (objiter ref = refs.begin(); ref != refs.end(); ++ref) {
	viccountry->resetLeaf((*ref)->getKey(), (*ref)->getLeaf()); 
      }
    }
    else {
      viccountry->resetLeaf("vote_franschise", "landed_voting");
    }

    // Reset to base
    viccountry->resetLeaf("slavery", "yes_slavery");
    viccountry->resetLeaf("upper_house_composition", "appointed");
    viccountry->resetLeaf("voting_system", "first_past_the_post");
    viccountry->resetLeaf("public_meetings", "no_meetings");
    viccountry->resetLeaf("press_rights", "state_press");
    viccountry->resetLeaf("trade_unions", "no_unions");
    viccountry->resetLeaf("political_parties", "underground_parties"); 
    
    Object* other = reforms->safeGetObject("other");
    objvec reformAreas = other->getLeaves();
    for (objiter ref = reformAreas.begin(); ref != reformAreas.end(); ++ref) {
      if ((*ref)->safeGetString("base", "NONE") == "NONE") continue; 
      double points = 0;
      double total = 1;
      Object* provEffects = (*ref)->safeGetObject("provinces");
      if (provEffects) {
	for (objiter p = eu3Provinces.begin(); p != eu3Provinces.end(); ++p) {
	  if (remQuotes((*p)->safeGetString("owner")) != eu3Country->getKey()) continue;
	  double tax = (*p)->safeGetFloat("base_tax");
	  total += tax;
	  points += tax*provEffects->safeGetFloat((*p)->safeGetString("trade_goods"));
	}
	points /= total; 
      }
      objvec slidEffects = (*ref)->getValue("slider");
      for (objiter slider = slidEffects.begin(); slider != slidEffects.end(); ++slider) {
	double amount = eu3Country->safeGetInt((*slider)->safeGetString("which"));
	amount = min(amount, (*slider)->safeGetFloat("max", 10));
	amount = max(amount, (*slider)->safeGetFloat("min", -10));
	amount *= (*slider)->safeGetFloat("value");
	points += amount; 
      }
      objvec ideaEffects = (*ref)->getValue("idea");
      for (objiter idea = ideaEffects.begin(); idea != ideaEffects.end(); ++idea) {
	if (eu3Country->safeGetString((*idea)->safeGetString("which")) == "yes") {
	  double value = (*idea)->safeGetFloat("value");
	  if (0 != value) points += value;
	  else {
	    value = (*idea)->safeGetFloat("mult");
	    if (0 != value) points *= value;
	  }
	}
      }

      objvec modEffects = (*ref)->getValue("modifier");
      objvec mods = eu3Country->getValue("modifier");
      for (objiter m = modEffects.begin(); m != modEffects.end(); ++m) {
	for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
	  if (remQuotes((*mod)->safeGetString("modifier")) != (*m)->safeGetString("mod")) continue;
	  points *= (*m)->safeGetFloat("mult"); 
	}
      }
      
      viccountry->resetLeaf((*ref)->getKey(), (*ref)->safeGetString("base"));
      objvec thresholds = (*ref)->getValue("threshold");
      double minimum = -1000000;
      for (objiter threshold = thresholds.begin(); threshold != thresholds.end(); ++threshold) {
	double currVal = (*threshold)->safeGetFloat("value");
	if (currVal < minimum) continue;
	if (currVal > points) continue;
	minimum = currVal;
	viccountry->resetLeaf((*ref)->getKey(), (*threshold)->safeGetString("rhs"));
      }
      
      Logger::logStream(Logger::Debug) << "Tag "
				       << viccountry->getKey() << " has "
				       << points
				       << " points for reform "
				       << (*ref)->getKey()
				       << ", set to "
				       << viccountry->safeGetString((*ref)->getKey())
				       << ".\n";

      
    }
    
    /*
      slavery=no_slavery
      vote_franschise=none_voting
      upper_house_composition=appointed
      voting_system=first_past_the_post
      public_meetings=yes_meeting
      press_rights=state_press
      trade_unions=no_trade_unions
      political_parties=underground_parties
    */
    
  }

  Logger::logStream(Logger::Game) << "Done with country mappings.\n";
}

double days (string datestring, int* getyear) {
  boost::char_separator<char> sep(".");
  boost::tokenizer<boost::char_separator<char> > tokens(datestring, sep);
  boost::tokenizer<boost::char_separator<char> >::iterator i = tokens.begin();
  int year = atoi((*i).c_str()); ++i;
  if (getyear) (*getyear) = year; 
  if (i == tokens.end()) {
    /*
    Logger::logStream(Logger::Warning) << "Attempt to use bad string \""
				       << datestring
				       << "\" as date, returning -1.\n";
    */ 
    return -1;
  }
  int month = atoi((*i).c_str()); ++i;
  if (i == tokens.end()) {
    /*
    Logger::logStream(Logger::Warning) << "Attempt to use bad string \""
				       << datestring
				       << "\" as date, returning -1.\n";
    */
    return -1;
  }
  int day = atoi((*i).c_str());

  double ret = day;
  ret += year*365;
  switch (month) { // Cannot add Dec, it'll be previous year
  case 12: ret += 30; // Nov
  case 11: ret += 31; // Oct
  case 10: ret += 30; // Sep
  case 9:  ret += 31; // Aug
  case 8:  ret += 31; // Jul
  case 7:  ret += 30; // Jun
  case 6:  ret += 31; // May
  case 5:  ret += 30; // Apr
  case 4:  ret += 31; // Mar
  case 3:  ret += 28; // Feb
  case 2:  ret += 31; // Jan 
  case 1:  // Nothing to add to previous year 
  default: break; 
  }
  return ret; 
}

string WorkerThread::getReligionAtDate (Object* eun, string date) {
  if (!eun) return "communist";
  int dateInDays = days(date);
  if (0 > dateInDays) {
    Logger::logStream(Logger::Warning) << "Cannot parse date "
				       << date
				       << " in attempting to determine religion; returning atheism.\n";
    return "atheist";
  }
  Object* history = eun->safeGetObject("history");
  if (!history) {
    Logger::logStream(Logger::Warning) << "No history for nation "
				       << eun->getKey()
				       << ", returning Jedi-ism.\n";
    return "jedi";
  }

  string ret = history->safeGetString("religion", "NONE");
  objvec dates = history->getLeaves();
  for (objiter d = dates.begin(); d != dates.end(); ++d) {
    int currDays = days((*d)->getKey());
    if (0 > currDays) continue;
    if (currDays > dateInDays) break;
    string religion = (*d)->safeGetString("religion", "NOCHANGE");
    if (religion == "NOCHANGE") continue;
    ret = religion; 
  }

  return ret;
}

string WorkerThread::nameAndNumber (Object* eu3prov) {
  return eu3prov->getKey() + " (" + eu3prov->safeGetString("name", "could not find name") + ")";
}

bool WorkerThread::isCoreAtDate(Object* eu3prov, string ownertag, string date) {
  Object* history = eu3prov->safeGetObject("history");
  if (!history) return false;
  bool ret = false;
  objvec adds = history->getValue("add_core");
  for (objiter a = adds.begin(); a != adds.end(); ++a) {
    if ((*a)->getLeaf() != ownertag) continue;
    ret = true;
    break; 
  }
  int coreDays = days(date);
  objvec dates = history->getLeaves();
  for (objiter d = dates.begin(); d != dates.end(); ++d) {
    int currDays = days((*d)->getKey());
    if (currDays < 0) continue;
    if (currDays > coreDays) return ret;
    if ((*d)->safeGetString("add_core") == ownertag) ret = true;
    if ((*d)->safeGetString("remove_core") == ownertag) ret = false;
  }
  return ret; 
}

bool WorkerThread::heuristicVicIsCoastal (Object* vicprov) {
  if (vicprov->safeGetObject("naval_base")) return true;
  Object* provInfo = getVicProvInfoFromVicProvince(vicprov);
  if (provInfo) {
    Object* buildingInfo = provInfo->safeGetObject("building_position");
    if ((buildingInfo) && (buildingInfo->safeGetObject("naval_base"))) return true;
  }
  return false; 
}

bool WorkerThread::heuristicIsCoastal (Object* eu3prov) {
  if (eu3prov->safeGetString("dock", "no") == "yes") return true;
  Object* provInfo = getEu3ProvInfoFromEu3Province(eu3prov);
  if ((provInfo) && (provInfo->safeGetObject("port"))) return true;
  return false; 
}

double WorkerThread::calculateReligionBadness (string provReligion, string stateReligion, string government) {
  if (provReligion == stateReligion) return 0;
  if (stateReligion == "NONE") return 0; 
  static int baseHeathen = 666;
  static int baseHeretic = -2;
  static double religionWeight = -0.333; 
  if (666 == baseHeathen) {
    Object* basevals = configObject->safeGetObject("base_values");
    if (basevals) {
      baseHeathen = basevals->safeGetInt("tolerance_heathen", -3);
      baseHeretic = basevals->safeGetInt("tolerance_heretic", -2);
    }
    else baseHeathen = -3; 
  }

  Object* prov = religionMap[provReligion];
  if (!prov) {
    Logger::logStream(Logger::Warning) << "Warning: Could not find province religion " << provReligion << ".\n";
    return baseHeathen*religionWeight; 
  }
  Object* stat = religionMap[stateReligion];
  if (!stat) {
    return baseHeathen*religionWeight; 
  }

  bool heretic = (prov->safeGetString("group", "christian") == stat->safeGetString("group", "christian"));
  double ret = heretic ? baseHeretic : baseHeathen;
  Object* religionMod = stat->safeGetObject("country");
  if (religionMod) ret += religionMod->safeGetInt(heretic ? "tolerance_heretic" : "tolerance_heathen");

  Object* gov = governmentMap[government];
  if (gov) ret += gov->safeGetInt(heretic ? "tolerance_heretic" : "tolerance_heathen");
  
  return ret*religionWeight; 
}

double WorkerThread::getPopWeight (Object* eu3prov, string poptype) {
 
  if (fullPopWeightDebug) {
    Object* popWeights = eu3prov->safeGetObject("popWeights");
    if (popWeights) {
      Logger::logStream(PopDebug) << "Pop weights object: " << popWeights << "\n"; 
    }
    eu3prov->unsetValue("popWeights");
  }

  if (cachedPopWeights) {
    Object* fromCache = cachedPopWeights->safeGetObject(eu3prov->getKey());
    if (fromCache) {
      fromCache->setKey("popWeights");
      eu3prov->setValue(fromCache); 
    }
  }

  bool debugProvince = eu3prov->safeGetString("debugPop", "no") == "yes";
  
  Object* popWeights = eu3prov->safeGetObject("popWeights");
  if (popWeights) { 
    return popWeights->safeGetFloat("overall")*popWeights->safeGetFloat(poptype, 1.0); 
  }

  popWeights = new Object("popWeights");
  eu3prov->setValue(popWeights); 
  
  double ret =  0;
  //Object* eu3Owner = eu3Game->safeGetObject(remQuotes(eu3prov->safeGetString("owner", "\"NONE\"")));
  
  double foreignOccupationDays = 0;
  double badReligionDays = 0;

  string startdate = remQuotes(eu3Game->safeGetString("start_date"));
  map<Object*, double> buildingDays; 

  double integratedProduction = 0;
  double integratedManpower = 0; 
  string tradegood = eu3prov->safeGetString("trade_goods", "NONE");
  int totalDays = 0;
  int prevDays = 0;
  double warFraction = 0;
  map<string, double> avgSliders; 
  if (debugProvince) {
    Logger::logStream(PopDebug) << "Integration for "
				<< nameAndNumber(eu3prov)
				<< " : \n"; 
  }

  string finalOwnerTag = "NONE"; 
  for (vector<ProvinceHistory*>::iterator period = eu3ProvinceToHistoryMap[eu3prov].begin(); period != eu3ProvinceToHistoryMap[eu3prov].end(); ++period) {
    ProvinceHistory* curr = (*period);
    finalOwnerTag = curr->ownerTag; 
    if (0 == (*period)->numDays) continue; 
    double buildingProductionMod   = 1;
    double prodEfficiency          = productionEfficiencies[(*period)->productionTech];
    double centralMod              = -0.03*(*period)->sliders["centralization_decentralization"];
    double govmentMod              = ((*period)->ownerGovernment == "NONE") ? 0 : governmentMap[(*period)->ownerGovernment]->safeGetFloat("production_efficiency");
    double deciderMod              = 0;
    for (objiter d = (*period)->modifiers.begin(); d != (*period)->modifiers.end(); ++d) {
      deciderMod += (*d)->safeGetFloat("production_efficiency"); 
    }
    
    prodEfficiency += centralMod + govmentMod + deciderMod; 
    
    double buildingProductionBonus = 0;
    double buildingManpowerBonus   = 0;
    double buildingManpowerMod     = 1; 
    for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
      if (!curr->buildings[(*b)->getKey()]) continue;
      Object* mod = (*b)->safeGetObject("modifier");
      bool hasEffect = false; 
      if (mod) {
	double temp = mod->safeGetFloat("local_production_efficiency");
	prodEfficiency += temp;
	if (temp > 0) hasEffect = true;

	temp = mod->safeGetFloat("local_trade_income_modifier");
	buildingProductionMod += temp;
	if (temp > 0) hasEffect = true;

	temp = mod->safeGetFloat("trade_income"); 
	buildingProductionBonus += temp;
	if (temp > 0) hasEffect = true;

	temp = mod->safeGetFloat("local_manpower"); 
	buildingManpowerBonus += temp; 
	if (temp > 0) hasEffect = true;

	temp = mod->safeGetFloat("local_manpower_modifier"); 
	buildingManpowerMod += temp; 
	if (temp > 0) hasEffect = true;
      }
      if (!hasEffect) buildingDays[*b] += curr->numDays;
    }
    if (curr->ownerTag != curr->controllerTag) foreignOccupationDays += curr->numDays;
    badReligionDays += curr->numDays*calculateReligionBadness(curr->religion, curr->ownerReligion, curr->ownerGovernment);
    if (prodEfficiency < 0) prodEfficiency = 0.01;

    for (objiter p = curr->triggerMods.begin(); p != curr->triggerMods.end(); ++p) {
      buildingProductionMod += (*p)->safeGetFloat("global_trade_income_modifier"); 
    }

    for (map<string, int>::iterator sl = curr->sliders.begin(); sl != curr->sliders.end(); ++sl) {
      avgSliders[(*sl).first] += (*sl).second * curr->numDays; 
    }
    
    totalDays += curr->numDays;
    double baseUnits = min(2.0, 0.99+curr->citysize/101000.0) + 0.05*curr->basetax;
    double currProduction = 0; 
    for (int i = prevDays; i < totalDays; ++i) {
      double price = 1;
      if ((int) prices[tradegood].size() > i) price = prices[tradegood][i];
      currProduction += (baseUnits*price + buildingProductionBonus)*buildingProductionMod*prodEfficiency;
    }
    integratedProduction += currProduction;

    if (isOverseasEu3Provinces(eu3prov, curr->capital)) buildingManpowerBonus -= 0.5; 
    
    double currManpower = 125*curr->manpower*baseUnits + 1000*buildingManpowerBonus; 
    currManpower *= buildingManpowerMod; 
    
    integratedManpower += currManpower*curr->numDays;
    if (curr->ownerAtWar) warFraction += curr->numDays;

    if (debugProvince) {
      Logger::logStream(PopDebug) << "  "
				  << curr->numDays << " days: MP "
				  << currManpower << " = ("
				  << 125*curr->manpower*baseUnits << " + " << 1000*buildingManpowerBonus << ") * "
				  << buildingManpowerMod
				  << ", prod "
				  << currProduction << " = (("
				  << baseUnits << " * " << prices[tradegood][prevDays + curr->numDays/2] << ") + "
				  << buildingProductionBonus << ") * "
				  << buildingProductionMod << " * " 
				  << prodEfficiency << " "
				  << curr->productionTech << " "
				  << curr->sliders["centralization_decentralization"] << " "
				  << centralMod << " "
				  << govmentMod << " "
				  << deciderMod 
				  << ".\n"; 
    }
    prevDays = totalDays; 
  } 
  
  integratedManpower /= totalDays;
  integratedProduction /= totalDays;
  integratedProduction /= eu3ProvinceToVicProvincesMap[eu3prov].size();
  integratedManpower /= eu3ProvinceToVicProvincesMap[eu3prov].size(); 

  integratedManpower *= 0.01; 
  
  ret = integratedManpower + integratedProduction; 
  warFraction /= totalDays; 
  
  foreignOccupationDays /= (gameDays - gameStartDays);
  badReligionDays       /= (gameDays - gameStartDays);

  for (map<string, double>::iterator s = avgSliders.begin(); s != avgSliders.end(); ++s) {
    avgSliders[(*s).first] /= totalDays; 
  }
  
  map<string, double> popTypeBonuses;
  for (map<string, Object*>::iterator p = poptypes.begin(); p != poptypes.end(); ++p) {
    double curr = 1; 
    for (map<string, double>::iterator s = avgSliders.begin(); s != avgSliders.end(); ++s) {
      curr += (*s).second * (*p).second->safeGetFloat((*s).first, 0); 
    }
    popTypeBonuses[(*p).first]  = curr;
    popTypeBonuses[(*p).first] *= (*p).second->safeGetFloat(eu3prov->safeGetString("trade_goods"), 1); 
  }
  
  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
    double fraction = 1;
    if (buildingDays[*b] > 0) {
      double firstDay = days((*b)->safeGetString("date", "1399.1.1"));
      if (firstDay > gameDays) fraction = 1.5; // Built before its time - maximum bonus 
      else fraction = min(1.5, buildingDays[*b] / (gameDays - firstDay));
      eu3prov->resetLeaf((*b)->getKey() + "_fraction", fraction); 
      fraction *= sqrt((*b)->safeGetFloat("cost", 50) / 1000);
      fraction += 1; 
      fraction *= (*b)->safeGetFloat("officials", 1);

      for (map<string, Object*>::iterator p = poptypes.begin(); p != poptypes.end(); ++p) {
	popTypeBonuses[(*p).first] += (*b)->safeGetFloat((*p).first) * (buildingDays[*b] / totalDays); 
      }
    }
    buildingDays[*b] = min(sqrt(fraction), 10.0); 
  }
  
  ret *= (1 - 0.9*foreignOccupationDays);
  ret *= (1 - 0.5*badReligionDays);

  popTypeBonuses["capitalists"] *= eu3prov->safeGetString("stock_exchange", "no") == "yes" ? 1.0 : 0.0; 
  //double factoryFactor = eu3prov->safeGetString("factory", "no") == "yes" ? 1.0 : 0.0;
  //popTypeBonuses["clerks"] *= factoryFactor;
  //popTypeBonuses["craftsmen"] *= factoryFactor;

  Object* finalOwner = findEu3CountryByTag(finalOwnerTag);
  if (finalOwner) {
    objvec mods = finalOwner->getValue("modifier");
    for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
      if (remQuotes((*mod)->safeGetString("modifier")) == "the_abolish_slavery_act") {
	popTypeBonuses["slaves"] = 0; 
      }
    }
  }
  
  Logger::logStream(PopDebug) << "Province "
			      << nameAndNumber(eu3prov) 
			      << " has properties: \n"
			      << "  Production : " << integratedProduction << "\n"
			      << "  Manpower   : " << integratedManpower << "\n"
			      << "  Occupation : " << (1-0.9*foreignOccupationDays) << "\n"
			      << "  Religion   : " << (1-0.5*badReligionDays) << "\n"
    //<< "  Wartime    : " << warFraction << "\n"
			      << "  Provinces  : " << (1.0/eu3ProvinceToVicProvincesMap[eu3prov].size()) << "\n";
    //<< "  Factories  : " << factoryFactor << "\n"; 
  double buildings = 1; 
  for (map<Object*, double>::iterator b = buildingDays.begin(); b != buildingDays.end(); ++b) {
    if ((*b).second == 1) continue;
    ret *= (*b).second;
    buildings *= (*b).second; 
    Logger::logStream(PopDebug) << "  " << (*b).first->getKey() << " : " << (*b).second << "\n";
  }

  if (!fullPopWeightDebug) {
    popWeights->resetLeaf("manpower", integratedManpower); 
    popWeights->resetLeaf("production", integratedProduction);
    double total = integratedProduction + integratedManpower;

    popWeights->resetLeaf("beforeOccupation", total); 
    total *= (1 - 0.9*foreignOccupationDays);
    popWeights->resetLeaf("beforeReligion", total);
    total *= (1 - 0.5*badReligionDays);
    popWeights->resetLeaf("beforeBuildings", total);
  }

  popWeights->resetLeaf("overall", ret);
  for (map<string, double>::iterator p = popTypeBonuses.begin(); p != popTypeBonuses.end(); ++p) {
    if (1 == (*p).second) continue;
    popWeights->resetLeaf((*p).first, (*p).second);
    Logger::logStream(PopDebug) << "  " << (*p).first << " : " << (*p).second << "\n"; 
  }
  
  Logger::logStream(PopDebug) << "  Popweight : " << ret << "\n"; 
  
  return ret*popWeights->safeGetFloat(poptype, 1.0); 
}


Object* WorkerThread::findEu3CountryByTag (string tag) {
  if (tag == "NONE") return 0;
  if (tag == "---") return 0;
  if (tag == "\"---\"") return 0; 
  Object* ret = eu3TagToEu3CountryMap[tag];
  if (ret) return ret;
  ret = eu3TagToEu3CountryMap[remQuotes(tag)];
  if (ret) return ret;
  ret = eu3Game->safeGetObject(tag);
  if (ret) return ret;
  ret = eu3Game->safeGetObject(remQuotes(tag));
  if (ret) return ret; 
  Logger::logStream(Logger::Error) << "Error: Could not find EU3 country \""
				   << tag
				   << "\".\n";
  return 0; 
}

Object* WorkerThread::findVicCountryByTag (string tag) {
  if (tag == "NONE") return 0; 
  Object* ret = vicTagToVicCountryMap[tag]; 
  if (ret) return ret;
  ret = vicTagToVicCountryMap[remQuotes(tag)];
  if (ret) return ret;
  ret = vicGame->safeGetObject(tag);
  if (ret) return ret;
  Logger::logStream(Logger::Error) << "Error: Could not find Vicky country \""
				   << tag
				   << "\".\n";
  assert(ret);
  return 0; 
}

Object* makeIdObject (int id, int type) {
  Object* ret = new Object("id");
  ret->setLeaf("id", id);
  ret->setLeaf("type", type);
  return ret; 
}

bool WorkerThread::redistPops () {
  return (configObject->safeGetString("redistPops", "no") == "yes"); 
}

void WorkerThread::reassignProvinces () {
  for (objiter vicprov = vicProvinces.begin(); vicprov != vicProvinces.end(); ++vicprov) {
    (*vicprov)->unsetValue("core");
    Object* rgo = (*vicprov)->safeGetObject("rgo");
    if (rgo) {
      rgo->unsetValue("last_income");
      Object* emp = rgo->safeGetObject("employment");
      Logger::logStream(Logger::Debug) << (*vicprov)->getKey() << " RGO\n";
      if (emp) {
	Logger::logStream(Logger::Debug) << (*vicprov)->getKey() << " emp\n";
	Object* empsObj = emp->safeGetObject("employees");
	if (empsObj) {
	  objvec emps = empsObj->getLeaves();
	  Logger::logStream(Logger::Debug) << (*vicprov)->getKey() << " emps " << (int) emps.size() << "\n";
	  if (0 < emps.size()) rgo->setLeaf("vanillaCount", max(emps[0]->safeGetInt("count"), 40000));
	  Logger::logStream(Logger::Debug) << "Vanilla count " << (*vicprov)->getKey() << " " << emps[0]->safeGetString("count") << "\n";
	  emp->unsetValue("employees");
	}
	/*objvec leaves = emp->getLeaves();
	Logger::logStream(Logger::Debug) << "  ";
	for (objiter i = leaves.begin(); i != leaves.end(); ++i) {
	  Logger::logStream(Logger::Debug) << (*i)->getKey() << " ";
	}
	Logger::logStream(Logger::Debug) << "\n";
	*/
      }
    }
    Object* eu3Prov = vicProvinceToEu3ProvincesMap[*vicprov][0];
    Object* eu3Owner = findEu3CountryByTag(eu3Prov->safeGetString("owner", "NONE"));
    if (!eu3Owner) {
      (*vicprov)->unsetValue("owner");
      (*vicprov)->unsetValue("controller");
      /*
      Logger::logStream(Logger::Debug) << "No Vic owner for Eu3 province "
				       << nameAndNumber(eu3Prov) << " "
				       << eu3Prov->safeGetString("owner", "NONE")
				       << ".\n";
      */ 
      continue;
    }
    if (0 == eu3CountryToVicCountryMap[eu3Owner]) {
      Logger::logStream(Logger::Debug) << "Error: Could not find Vicky countries for EU3 tag "
				       << eu3Owner->getKey()
				       << "\n";
      continue; 
    }
    (*vicprov)->resetLeaf("owner", addQuotes(eu3CountryToVicCountryMap[eu3Owner]->getKey()));
    eu3Owner = findEu3CountryByTag(eu3Prov->safeGetString("controller"));
    if ((!eu3Owner) || (0 == eu3CountryToVicCountryMap[eu3Owner])) {
      Logger::logStream(Logger::Debug) << "Error: Could not find Vicky countries for EU3 tag "
				       << eu3Owner->getKey()
				       << "\n";
      (*vicprov)->resetLeaf("controller", (*vicprov)->safeGetString("owner"));
      continue; 
    }
    (*vicprov)->resetLeaf("controller", addQuotes(eu3CountryToVicCountryMap[eu3Owner]->getKey()));
  }
  
  int stateIndex = 0;
  for (objiter region = regions.begin(); region != regions.end(); ++region) {
    map<Object*, Object*> ownerToStateMap; 
    for (int i = 0; i < (*region)->numTokens(); ++i) {
      Object* prov = vicGame->safeGetObject((*region)->getToken(i));
      if (!prov) continue;
      string ownertag = remQuotes(prov->safeGetString("owner", "\"NONE\""));
      if (ownertag == "NONE") continue;
      Object* owner = vicGame->safeGetObject(ownertag);
      if (!owner) {
	Logger::logStream(Logger::Error) << "Error: Province "
					 << nameAndNumber(prov)
					 << " has nonexistent owner "
					 << ownertag
					 << ".\n";
	continue; 
      }
      Object* eu3owner = findEu3CountryByVicCountry(owner); 

      if (!ownerToStateMap[owner]) {
	ownerToStateMap[owner] = new Object("state");
	ownerToStateMap[owner]->setValue(makeIdObject(stateIndex++, 47));
	Object* provs = new Object("provinces");
	provs->setObjList();
	ownerToStateMap[owner]->setValue(provs);
	owner->setValue(ownerToStateMap[owner]);
      }
      Object* provs = ownerToStateMap[owner]->safeGetObject("provinces"); 
      provs->addToList(prov->getKey());
      
      bool colony = true; 	
      if (!isOverseasVicProvinces(prov, vicGame->safeGetObject(owner->safeGetString("capital", "-1")))) colony = false;
      else {
	double ownedSinceForever = 0; 
	for (objiter eu3prov = vicProvinceToEu3ProvincesMap[prov].begin(); eu3prov != vicProvinceToEu3ProvincesMap[prov].end(); ++eu3prov) {
	  bool debugColony = ((*eu3prov)->safeGetString("debugColony", "no") == "yes"); 
	  
	  double totalDays = 0;
	  double ownerDays = 0;
	  double ownedDays = 0; 
	  for (vector<ProvinceHistory*>::iterator i = eu3ProvinceToHistoryMap[*eu3prov].begin(); i != eu3ProvinceToHistoryMap[*eu3prov].end(); ++i) {
	    totalDays += (*i)->numDays;
	    if ((*i)->ownerTag == eu3owner->getKey()) ownerDays += (*i)->numDays;
	    if ((*i)->ownerTag != "NONE") ownedDays += (*i)->numDays;	    
	  }
	  ownerDays /= totalDays;
	  ownedDays /= totalDays;
	  double required = 0.66;

	  string finalCulture = (*eu3prov)->safeGetString("culture");
	  string startCulture = (*eu3prov)->safeGetObject("history")->safeGetString("culture");
	  string eu3ownertag = (*eu3prov)->safeGetString("owner");
	  objvec accepted = eu3owner->getValue("accepted_culture"); 
	  //objvec accepted = findEu3CountryByTag((*eu3prov)->safeGetString("owner"))->getValue("accepted_culture");
	  for (objiter acc = accepted.begin(); acc != accepted.end(); ++acc) {
	    if (((*acc)->getLeaf() != finalCulture) && ((*acc)->getLeaf() != startCulture)) continue;
	    required = 0.33;
	    break;
	  }

	  if (debugColony) {
	    Logger::logStream(Logger::Debug) << "Colony debug: EU3 province "
					     << (*eu3prov)->getKey() << " " 
					     << ownerDays << " "
					     << ownedDays << " "
					     << required
					     << ".\n"; 
	  }
	  
	  if (ownerDays > required*ownedDays) ownedSinceForever++;
	}
	if (0.66 <= ownedSinceForever / vicProvinceToEu3ProvincesMap[prov].size()) colony = false; 
      }
      if (colony) ownerToStateMap[owner]->resetLeaf("colonies", 1 + ownerToStateMap[owner]->safeGetInt("colonies", 0));
      ownerToStateMap[owner]->resetLeaf("total", 1 + ownerToStateMap[owner]->safeGetInt("total", 0));
    }
    for (map<Object*, Object*>::iterator state = ownerToStateMap.begin(); state != ownerToStateMap.end(); ++state) {
      if (0.5 < (*state).second->safeGetFloat("colonies") / (*state).second->safeGetFloat("total")) (*state).second->resetLeaf("is_colonial", "yes");
      (*state).second->unsetValue("total");
      (*state).second->unsetValue("colonies"); 
    }
  }
  vicGame->resetLeaf("state", stateIndex); 
}

void WorkerThread::redistributeGoods () {
  for (map<string, double>::iterator goods = goodsToRedistribute.begin(); goods != goodsToRedistribute.end(); ++goods) {
    Logger::logStream(Logger::Game) << (*goods).first << " : " << (*goods).second << "\n"; 
  }
  
  Object* extraGoods = configObject->safeGetObject("extraGoods");
  if (extraGoods) {
    objvec extras = extraGoods->getLeaves();
    for (objiter extra = extras.begin(); extra != extras.end(); ++extra) {
      goodsToRedistribute[(*extra)->getKey()] += extraGoods->safeGetFloat((*extra)->getKey()); 
    }
  }
  
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* eu3 = findEu3CountryByVicCountry(*vic);
    if (!eu3) continue;
    double tradePower = eu3->safeGetFloat("totalTrade", -1);
    if (0 > tradePower) continue;
    Object* stockpile = (*vic)->safeGetObject("stockpile");
    if (!stockpile) {
      stockpile = new Object("stockpile");
      (*vic)->setValue(stockpile);
    }
    for (map<string, double>::iterator stock = goodsToRedistribute.begin(); stock != goodsToRedistribute.end(); ++stock) {
      if ((*stock).first == "total") continue; 
      stockpile->resetLeaf((*stock).first, (*stock).second * tradePower + stockpile->safeGetFloat((*stock).first)); 
    }
  }
}

Object* WorkerThread::findVicCountryByEu3Country (Object* eu3) {
  return eu3CountryToVicCountryMap[eu3]; 
}

Object* WorkerThread::findEu3CountryByVicCountry (Object* vic) {
  if (!vic) return 0; 
  return vicCountryToEu3CountriesMap[vic][0]; 
}

string WorkerThread::findVicTagFromEu3Tag (string eu3tag, bool quotes) {
  Object* eu3Country = findEu3CountryByTag(eu3tag);
  Object* vicCountry = findVicCountryByEu3Country(eu3Country);
  if (!vicCountry) return "NONE"; 
  string ret = vicCountry->getKey();
  if (!quotes) return ret;
  return addQuotes(ret); 
}

struct IndustryStrength {
  double production;
  double industry; 
};

void WorkerThread::addFactory (Object* vicNation, Object* factory) {
  objvec states = vicNation->getValue("state");
  if (0 == states.size()) {
    Logger::logStream(Logger::Warning) << "Attempt to give factory to nation "
				       << vicNation->getKey()
				       << ", which has no states and industry "
				       << vicNation->safeGetString("industrialStrength")
				       << "\n";
    return; 
  }
  Object* bestState = states[0];
  double bestWeight = 0; 
  for (objiter state = states.begin(); state != states.end(); ++state) {
    Object* vicList = (*state)->safeGetObject("provinces");
    double totalWeight = 0;
    for (int i = 0; i < vicList->numTokens(); ++i) {
      Object* p = vicGame->safeGetObject(vicList->getToken(i));
      if (!p) continue; 

      double multiplier = 1; 
      if (isOverseasVicProvinces(p, vicGame->safeGetObject(vicNation->safeGetString("capital", "-1")))) multiplier = 0.25;
      for (objiter eu3prov = vicProvinceToEu3ProvincesMap[p].begin(); eu3prov != vicProvinceToEu3ProvincesMap[p].end(); ++eu3prov) {
	totalWeight += 0.05*(*eu3prov)->safeGetFloat("base_tax") * multiplier;
	totalWeight += max(2.0, 0.99+(*eu3prov)->safeGetFloat("citysize")/101000.0) * multiplier;
      }
    }
    totalWeight *= pow(0.9, (*state)->getValue("state_buildings").size());
    if (totalWeight < bestWeight) continue;
    bestWeight = totalWeight;
    bestState = (*state); 
  }
  if (!bestState) {
    Logger::logStream(Logger::Warning) << "Unable to assign factory to any state in "
				       << vicNation->getKey()
				       << ".\n";
    return; 
  }

  Object* existingFactory = 0;
  objvec facs = bestState->getValue("state_buildings");
  for (objiter f = facs.begin(); f != facs.end(); ++f) {
    if ((*f)->safeGetString("building") != factory->safeGetString("building")) continue;
    existingFactory = (*f);
    break; 
  }
  if (!existingFactory) bestState->setValue(factory);
  else existingFactory->resetLeaf("level", existingFactory->safeGetInt("level")+1);

  Object* vicList = bestState->safeGetObject("provinces");
  for (int i = 0; i < vicList->numTokens(); ++i) {
    Object* p = vicGame->safeGetObject(vicList->getToken(i));
    if (!p) continue; 
    p->resetLeaf("factory", "yes");
    vicProvinceToEu3ProvincesMap[p][0]->resetLeaf("factory", "yes"); 
  }
  vicNation->resetLeaf("numFactories", 1 + vicNation->safeGetInt("numFactories")); 
}

double traitGoodness (Object* trait) {
  double attack = trait->safeGetFloat("attack");
  double defence = trait->safeGetFloat("defence");
  double morale = trait->safeGetFloat("morale");
  double organisation = trait->safeGetFloat("organisation");
  double reconnaissance = trait->safeGetFloat("reconnaisance");
  double speed = trait->safeGetFloat("speed");
  double attrition = trait->safeGetFloat("attrition");
  double experience = trait->safeGetFloat("experience");
  double reliability = trait->safeGetFloat("reliability");


  double ret = attack + defence;
  ret += 3 * morale;
  ret += 3 * organisation;
  ret += 3 * reconnaissance;
  ret += 10 * speed;
  ret -= 100 * attrition; // Negative attrition modifier is good! 
  ret += 10 * experience;
  ret += 10 * reliability; 
  return ret; 
}

void WorkerThread::leaders () {
  int leadernum = 1;
  Object* person = traitObject->safeGetObject("personality");
  objvec personalities = person->getLeaves();
  Object* backgr = traitObject->safeGetObject("background");
  objvec backgrounds   = backgr->getLeaves();

  Logger::logStream(Logger::Debug) << "Number of traits: " << (int) backgrounds.size() << " " << (int) personalities.size() << "\n"; 
  
  double maxPersonGoodness = 0;
  double minPersonGoodness = 0;
  for (objiter p = personalities.begin(); p != personalities.end(); ++p) {
    double curr = traitGoodness(*p);
    if (curr < minPersonGoodness) minPersonGoodness = curr;
    if (curr > maxPersonGoodness) maxPersonGoodness = curr; 
  }
  maxPersonGoodness -= minPersonGoodness;
  
  double maxBackgrGoodness = 0;
  double minBackgrGoodness = 0;
  for (objiter p = backgrounds.begin(); p != backgrounds.end(); ++p) {
    double curr = traitGoodness(*p);
    if (curr > maxBackgrGoodness) maxBackgrGoodness = curr;
    if (curr < minBackgrGoodness) minBackgrGoodness = curr; 
  }
  maxBackgrGoodness -= minBackgrGoodness; 
  
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* eu3 = findEu3CountryByVicCountry(*vic);
    if (!eu3) {
      (*vic)->resetLeaf("leadership", "0.000");
      continue; 
    }

    (*vic)->resetLeaf("leadership", 100*(eu3->safeGetFloat("army_tradition") + eu3->safeGetFloat("navy_tradition")));
    (*vic)->unsetValue("leader"); 
			 
    if (0 == personalities.size()) continue;
    
    objvec leaderIds = eu3->getValue("leader");
    for (objiter leaderId = leaderIds.begin(); leaderId != leaderIds.end(); ++leaderId) {
      string id = (*leaderId)->safeGetString("id");
      Object* eu3leader = leaderMap[id];
      if (!eu3leader) continue;

      Object* vicleader = new Object("leader");
      (*vic)->setValue(vicleader);
      vicleader->resetLeaf("name", eu3leader->safeGetString("name"));
      vicleader->resetLeaf("date", eu3leader->safeGetString("activation"));
      bool general = (eu3leader->safeGetString("type") == "general");
      vicleader->resetLeaf("type", general ? "land" : "sea");
      vicleader->resetLeaf("country", addQuotes((*vic)->getKey()));
      Object* idobj = new Object("id");
      vicleader->setValue(idobj);
      idobj->resetLeaf("id", leadernum++);
      idobj->resetLeaf("type", "37");
      double picnum = rand();
      picnum /= RAND_MAX;
      picnum *= (general ? 57 : 31);
      sprintf(strbuffer, "\"european_%s_%i\"", (general ? "general" : "admiral"), (int) floor(picnum));
      vicleader->resetLeaf("picture", strbuffer);

      double totalGoodness = eu3leader->safeGetFloat("shock");
      totalGoodness       += eu3leader->safeGetFloat("fire");
      totalGoodness       += eu3leader->safeGetFloat("manuever"); // Misspelling in EU3 saves
      totalGoodness       += eu3leader->safeGetFloat("siege");

      totalGoodness *= 0.05; // Gives 1 for maximum possible skills, 6-6-6-2
      int printed = 0; 
      while (true) {
	double curr = rand(); 
	int pIndex = floor((curr / RAND_MAX) * personalities.size());
	curr = traitGoodness(personalities[pIndex]);
	curr -= minPersonGoodness; 
	curr /= maxPersonGoodness;
	curr = exp(-3 * fabs(curr - totalGoodness));
	if (10 > printed++) Logger::logStream(Logger::Debug) << "Index "
							     << pIndex << " "
							     << curr << " "
							     << traitGoodness(personalities[pIndex]) << " "
							     << "\n"; 
			      
	if (curr < (rand() / RAND_MAX)) continue;
	vicleader->resetLeaf("personality", addQuotes(personalities[pIndex]->getKey()));
	break; 	
      }
      while (true) {
	double curr = rand(); 
	int pIndex = floor((curr / RAND_MAX) * backgrounds.size());
	curr = traitGoodness(backgrounds[pIndex]);
	curr -= minBackgrGoodness; 		
	curr /= maxBackgrGoodness;
	curr = exp(-3 * fabs(curr - totalGoodness));
	if (curr < (rand() / RAND_MAX)) continue;
	vicleader->resetLeaf("background", addQuotes(backgrounds[pIndex]->getKey()));
	break; 
      }
      
      
    }
  }
}

void WorkerThread::moveFactories () {
  map<Object*, IndustryStrength*> indMap; 
  for (objiter eu3 = eu3Provinces.begin(); eu3 != eu3Provinces.end(); ++eu3) {
    Object* eu3Owner = findEu3CountryByTag((*eu3)->safeGetString("owner"));
    if (!eu3Owner) continue;

    if (!indMap[eu3Owner]) indMap[eu3Owner] = new IndustryStrength();

    double prod = 0.05*(*eu3)->safeGetFloat("base_tax");
    prod += max(2.0, 0.99+(*eu3)->safeGetFloat("citysize")/101000.0);
    indMap[eu3Owner]->production += prod;
    for (vector<ProvinceHistory*>::iterator per = eu3ProvinceToHistoryMap[*eu3].begin(); per != eu3ProvinceToHistoryMap[*eu3].end(); ++per) {
      if ((*per)->buildings["textile"])  indMap[eu3Owner]->industry += (*per)->numDays;
      if ((*per)->buildings["weapons"])  indMap[eu3Owner]->industry += (*per)->numDays;
      if ((*per)->buildings["wharf"])    indMap[eu3Owner]->industry += (*per)->numDays;
      if ((*per)->buildings["refinery"]) indMap[eu3Owner]->industry += (*per)->numDays;
    }
  }

  Logger::logStream(Logger::Game) << "Initial industrial strengths: \n";
  for (map<Object*, IndustryStrength*>::iterator eu3 = indMap.begin(); eu3 != indMap.end(); ++eu3) {
    Object* vic = findVicCountryByEu3Country((*eu3).first);
    if (!vic) continue;
    double strength = (*eu3).second->industry / (1 + (*eu3).second->production);
    vic->resetLeaf("industrialStrength", max(strength, vic->safeGetFloat("industrialStrength"))); 

    Logger::logStream(Logger::Game) << "  " << (*eu3).first->getKey()
				    << " : " << strength << " "
				    << (*eu3).second->industry << " "
				    << (*eu3).second->production << " " 
				    << vic->getKey() 
				    << "\n"; 
  }

  ObjectDescendingSorter sorter("industrialStrength");
  for (objiter vf = vicFactories.begin(); vf != vicFactories.end(); ++vf) {
    sort(vicCountries.begin(), vicCountries.end(), sorter);
    Object* recipient = vicCountries[0];
    recipient->resetLeaf("industrialStrength", 0.50*recipient->safeGetFloat("industrialStrength"));
    addFactory(recipient, (*vf)); 
  }
}


void swapRgos (Object* oneProv, Object* twoProv) {
  Object* rgo1 = oneProv->safeGetObject("rgo");
  Object* rgo2 = twoProv->safeGetObject("rgo");
  oneProv->unsetValue("rgo");
  twoProv->unsetValue("rgo");
  oneProv->setValue(rgo2);
  twoProv->setValue(rgo1);
  string oneFarmers = oneProv->safeGetString("canHaveFarmers", "no");
  oneProv->resetLeaf("canHaveFarmers", twoProv->safeGetString("canHaveFarmers", "no"));
  twoProv->resetLeaf("canHaveFarmers", oneFarmers);

  Object* emp = rgo1->safeGetObject("employment");
  if (emp) {
    emp->resetLeaf("province_id", twoProv->getKey());
    emp->unsetValue("employees"); // Provinces brought in from EU3 wasteland will still have their employees set. 
  }
  emp = rgo2->safeGetObject("employment");
  if (emp) {
    emp->resetLeaf("province_id", oneProv->getKey());
    emp->unsetValue("employees"); 
  }
}

void WorkerThread::moveRgosWithList () {
  if (!rgoList) return;
  objvec leaves = rgoList->getLeaves();
  //int counter = 0; 
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    Object* oneProv = vicGame->safeGetObject((*leaf)->getKey());
    Object* twoProv = vicGame->safeGetObject((*leaf)->getLeaf());
    if (oneProv == twoProv) continue; 
    if ((!oneProv) || (!twoProv)) {
      Logger::logStream(Logger::Warning) << "Warning: Could not perform switch " << (*leaf)->getKey() <<  " <-> " << (*leaf)->getLeaf() << "\n";
      continue;
    }
    //if (21 < counter) break; 
    //counter++;
    //Logger::logStream(Logger::Game) << "Swapped " << (*leaf)->getKey() <<  " <-> " << (*leaf)->getLeaf() << "\n";
    
    swapRgos(oneProv, twoProv); 
  }
}

void WorkerThread::moveRgos () {
  string regionType = configObject->safeGetString("rgoRedist", "none");
  if (regionType == "none") return;

  if (regionType == "list") {
    moveRgosWithList();
    return; 
  }

  // Redistribute RGO goods within regions.  
  map<string, objvec> regionMap;
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) regionMap[(*vp)->safeGetString(regionType)].push_back(*vp);

  for (map<string, objvec>::iterator region = regionMap.begin(); region != regionMap.end(); ++region) {
    Logger::logStream(Logger::Game) << "Redistributing RGOs in region " << (*region).first << ".\n";
    objvec oldOrder = (*region).second;
    objvec newOrder;

    while (0 < oldOrder.size()) {
      int idx = rand();
      idx %= oldOrder.size();
      newOrder.push_back(oldOrder[idx]);
      oldOrder[idx] = oldOrder.back();
      oldOrder.pop_back(); 
    }

    oldOrder = (*region).second;
    for (unsigned int i = 0; i < oldOrder.size(); ++i) {
      if (oldOrder[i] == newOrder[i]) continue;
      swapRgos(oldOrder[i], newOrder[i]);
    }
  }
}

const int numGoodsFirstHalf = 13; 

void WorkerThread::printPopInfo (PopSourceInfo* dat, bool firstHalf) {
  Logger::logStream(PopDebug) << dat->tag << "\t"; 
  
  static char tempbuffer[1000]; 
  if (firstHalf) {
    dat->totalMP = 0;
    dat->totalPR = 0; 
    for (int g = 0; g < numGoodsFirstHalf; ++g) {
      sprintf(tempbuffer, "%.1f/%.1f", 0.001*dat->goods[goods[g]->getKey()], 0.001*dat->manpower[goods[g]->getKey()]);
      sprintf(strbuffer, "%-10s ", tempbuffer); 
      Logger::logStream(PopDebug) << strbuffer;
      dat->totalMP += dat->manpower[goods[g]->getKey()];
      dat->totalPR += dat->goods[goods[g]->getKey()];
    }
    Logger::logStream(PopDebug) << "\n";
  }
  else {
    for (unsigned int g = numGoodsFirstHalf; g < goods.size(); ++g) {
      sprintf(tempbuffer, "%.1f/%.1f", 0.001*dat->goods[goods[g]->getKey()], 0.001*dat->manpower[goods[g]->getKey()]);
      sprintf(strbuffer, "%-10s ", tempbuffer); 
      Logger::logStream(PopDebug) << strbuffer;
      dat->totalMP += dat->manpower[goods[g]->getKey()];
      dat->totalPR += dat->goods[goods[g]->getKey()];
    }
    sprintf(strbuffer,
	    "\t %.1f\t %.1f\t %.1f%%\t %.1f%%\t %.1f%%\t %.1f",
	    0.001*dat->totalMP,
	    0.001*dat->totalPR, 
	    100*(dat->occupation.second / dat->occupation.first),
	    100*(dat->religion.second   / dat->religion.first),
	    100*(dat->buildings.second  / dat->buildings.first),
	    0.001*dat->final);
    Logger::logStream(PopDebug) << strbuffer << "\n";
  }
}

void PopSourceInfo::add (PopSourceInfo* dat) {
  for (map<string, double>::iterator m = dat->manpower.begin(); m != dat->manpower.end(); ++m) {
    manpower[(*m).first] += (*m).second; 
  }
  for (map<string, double>::iterator m = dat->goods.begin(); m != dat->goods.end(); ++m) {
    goods[(*m).first] += (*m).second; 
  }
  occupation.first += dat->occupation.first;
  religion.first += dat->religion.first;
  buildings.first += dat->buildings.first;
  occupation.second += dat->occupation.second;
  religion.second += dat->religion.second;
  buildings.second += dat->buildings.second;
  final += dat->final; 
}

void WorkerThread::ProvinceGroup::printLiteracy () {
  if (boss->configObject->safeGetString("debug", "no") != "yes") return;
  
  for (vector<PopInfo*>::iterator pinfo = popinfos.begin(); pinfo != popinfos.end(); ++pinfo) {
    double checkIntegral = 0;
    double sizeIntegral = 0; 
    Logger::logStream(Logger::Debug) << (*pinfo)->ptype
				     << " literacy : ";
    for (objiter vprov = vicProvs.begin(); vprov != vicProvs.end(); ++vprov) {
      objvec pops = (*vprov)->getValue((*pinfo)->ptype); 
      for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
	double size = (*pop)->safeGetFloat("size");
	double lit  = (*pop)->safeGetFloat("literacy");
	checkIntegral += size*lit;
	sizeIntegral += size; 
      }
    }
    Logger::logStream(Logger::Debug) << (*pinfo)->integratedLiteracy << " "
				     << (*pinfo)->worldTotal << " "
				     << ((*pinfo)->integratedLiteracy / (*pinfo)->worldTotal) << " " 
				     << checkIntegral << " "
				     << sizeIntegral << " "
				     << (checkIntegral / sizeIntegral) << " "
				     << ((*pinfo)->worldTotal / sizeIntegral) << " " 
				     << "\n"; 
    
  }
}

void WorkerThread::movePops () {
  if (!redistPops()) return; 
  int popid = vicGame->safeGetInt("start_pop_index");

  for (vector<ProvinceGroup*>::iterator pgroup = provinceGroups.begin(); pgroup != provinceGroups.end(); ++pgroup) {
    (*pgroup)->printLiteracy();
    (*pgroup)->redistribute(popid);
    (*pgroup)->moveLiteracy();
    (*pgroup)->printLiteracy();
  }
  
  vicGame->resetLeaf("start_pop_index", popid);

  PopSourceInfo* nonHumans = new PopSourceInfo();
  nonHumans->tag = "NPC"; 
  
  Logger::logStream(PopDebug) << "TAG\t";
  for (int i = 0; i < numGoodsFirstHalf; ++i) {
    sprintf(strbuffer, "%-10.10s", goods[i]->getKey().c_str());
    Logger::logStream(PopDebug) << strbuffer; 
  }
  Logger::logStream(PopDebug) << "\n"; 

  vector<PopSourceInfo*> humanTags; 
  
  for (map<Object*, PopSourceInfo*>::iterator i = eu3CountryToPopSourceMap.begin(); i != eu3CountryToPopSourceMap.end(); ++i) {
    if ((*i).first->safeGetString("human", "no") != "yes") {
      nonHumans->add((*i).second); 
      continue;
    }
    humanTags.push_back((*i).second);
  }

  sort(humanTags.begin(), humanTags.end(), *nonHumans); 
  for (vector<PopSourceInfo*>::iterator psi = humanTags.begin(); psi != humanTags.end(); ++psi) {
    printPopInfo((*psi), true);
  }
  printPopInfo(nonHumans, true);
  
  Logger::logStream(PopDebug) << "\nTAG\t";
  for (unsigned int i = numGoodsFirstHalf; i < goods.size(); ++i) {
    sprintf(strbuffer, "%-10.10s", goods[i]->getKey().c_str());
    Logger::logStream(PopDebug) << strbuffer; 
  }
  
  Logger::logStream(PopDebug) << "\t MP \t Prod\t Occu\t Reli\t Build\t Total\n"; 


  for (vector<PopSourceInfo*>::iterator psi = humanTags.begin(); psi != humanTags.end(); ++psi) {
    printPopInfo((*psi), false);
  }
  printPopInfo(nonHumans, false);

  
  if ((configObject->safeGetString("getPopWeights", "calculate") == "calculate") &&
      (configObject->safeGetString("writeCache", "no") == "yes")) {

    ofstream tradewriter;
    tradewriter.open("./priceCache/popWeights.txt");

    for (objiter eu3 = eu3Provinces.begin(); eu3 != eu3Provinces.end(); ++eu3) {
      Object* toCache = (*eu3)->safeGetObject("popWeights");
      if (!toCache) continue;
      toCache->setKey((*eu3)->getKey());
      tradewriter << *toCache << "\n"; 
    }
    tradewriter.close();     
  }
}

Object* WorkerThread::convertDipObject (Object* edip) {
  // Converts alliance or vassalisation. Returns 0 if invalid.
  // Invalid objects either have a nation allied to itself, or
  // the EU3 nation is a secondary, ie, it is not the first in
  // the conversion list. 
  
  Object* vdip = new Object(edip->getKey());
  string first  = findVicTagFromEu3Tag(edip->safeGetString("first", "NONE"), true);
  string eu3sec = edip->safeGetString("second", "NONE");
  string second = findVicTagFromEu3Tag(eu3sec, true);
  if (first == second) return 0;
  Object* eu3country = findEu3CountryByTag(eu3sec);
  Object* viccountry = findVicCountryByEu3Country(eu3country);
  if (eu3country != findEu3CountryByVicCountry(viccountry)) return 0; 
  vdip->setLeaf("first", first);
  vdip->setLeaf("second", second);
  vdip->setLeaf("start_date", edip->safeGetString("start_date", "\"1836.1.1\""));
  return vdip; 
}

void WorkerThread::diplomacy () {
  Object* vdip = vicGame->safeGetObject("diplomacy");
  if (!vdip) {
    vdip = new Object("diplomacy");
    vicGame->setValue(vdip); 
  }
  vdip->unsetValue("vassal");
  vdip->unsetValue("alliance");
  
  Object* edip = eu3Game->safeGetObject("diplomacy");
  if (!edip) return;
  objvec vassals = edip->getValue("vassal");
  for (objiter v = vassals.begin(); v != vassals.end(); ++v) {
    Object* n = convertDipObject(*v);
    if (n) vdip->setValue(n);
    Object* vassal = eu3Game->safeGetObject(remQuotes((*v)->safeGetString("second")));
    if (vassal) vassal->resetLeaf("isVassal", "yes"); 
  }
  objvec alliances = edip->getValue("alliance");
  for (objiter v = alliances.begin(); v != alliances.end(); ++v) {
    Object* n = convertDipObject(*v);
    if (n) vdip->setValue(n);
  }


  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    double power = (*vic)->safeGetFloat("prestige") + (*vic)->safeGetFloat("numFactories") + (*vic)->safeGetFloat("numUnits");
    (*vic)->resetLeaf("powerScore", power); 
  }

  ObjectDescendingSorter sorter("powerScore");
  sort(vicCountries.begin(), vicCountries.end(), sorter); 
  
  objvec greatPowers;
  Logger::logStream(Logger::Debug) << "Sorted GP list: ";
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* eu3 = findEu3CountryByVicCountry(*vic);
    if (eu3->safeGetString("isVassal", "no") == "yes") continue;
    if (find(greatPowers.begin(), greatPowers.end(), (*vic)) != greatPowers.end()) continue; 
    greatPowers.push_back(*vic);
    Logger::logStream(Logger::Debug) << (*vic)->getKey() << " ("
				     << (*vic)->safeGetFloat("powerScore") << " "
				     << (*vic)->safeGetFloat("prestige") << " "
				     << (*vic)->safeGetFloat("numFactories") << " "
				     << (*vic)->safeGetFloat("numUnits") << ") "; 
    (*vic)->resetLeaf("greatPower", "yes"); 
    if (8 <= greatPowers.size()) break;
  }
  Logger::logStream(Logger::Debug) << "\n"; 
  
  vicGame->unsetValue("great_nations");
  Object* vicGPs = new Object("great_nations");
  vicGPs->setObjList();
  objvec vicLeaves = vicGame->getLeaves();
  int firstNationIndex = 0;
  for (unsigned int i = 0; i < vicLeaves.size(); ++i) {
    if (!vicLeaves[i]->safeGetObject("flags")) continue;
    firstNationIndex = i;
    break; 
  }

  for (objiter gp = greatPowers.begin(); gp != greatPowers.end(); ++gp) {
    Object* vic = (*gp); 
    //Logger::logStream(Logger::Debug) << "Index of Vic nation " << vic->getKey(); 
    
    for (unsigned int i = 0; i + firstNationIndex < vicLeaves.size(); ++i) {
      //Logger::logStream(Logger::Debug) << vicLeaves[i+firstNationIndex]->getKey() << " "; 
      if (vicLeaves[i+firstNationIndex] != vic) continue;
      sprintf(strbuffer, "%i", i+1); // Fortran indexing
      vicGPs->addToList(strbuffer);
      //Logger::logStream(Logger::Debug) << " " << strbuffer; 
      break;
    }
    //Logger::logStream(Logger::Debug) << "\n"; 
  }
  vicGame->setValue(vicGPs); 
  
  
  objvec wars = eu3Game->getValue("active_war");
  for (objiter war = wars.begin(); war != wars.end(); ++war) {
    objvec attackers = (*war)->getValue("attacker");
    objvec defenders = (*war)->getValue("defender");
    objvec vicAtt;
    objvec vicDef;
    for (objiter att = attackers.begin(); att != attackers.end(); ++att) {
      Object* eu3 = eu3Game->safeGetObject(remQuotes((*att)->getLeaf()));
      if (!eu3) continue;
      Object* vic = findVicCountryByEu3Country(eu3);
      if (!vic) continue;
      if (find(vicAtt.begin(), vicAtt.end(), vic) != vicAtt.end()) continue;
      vicAtt.push_back(vic);
    }
    for (objiter def = defenders.begin(); def != defenders.end(); ++def) {
      Object* eu3 = eu3Game->safeGetObject(remQuotes((*def)->getLeaf()));
      if (!eu3) continue;
      Object* vic = findVicCountryByEu3Country(eu3);
      if (!vic) continue;
      if (find(vicDef.begin(), vicDef.end(), vic) != vicDef.end()) continue;
      if (find(vicAtt.begin(), vicAtt.end(), vic) != vicAtt.end()) continue; // Nobody at war with themselves, please...
      vicDef.push_back(vic);
    }

    if (0 == vicAtt.size()) continue;
    if (0 == vicDef.size()) continue;
    
    Object* vicWar = new Object("active_war");
    vicGame->setValue(vicWar); 
    vicWar->setLeaf("name", (*war)->safeGetString("name", "\"Random War\""));
    Object* history = new Object("history");
    vicWar->setValue(history);
    for (objiter att = vicAtt.begin(); att != vicAtt.end(); ++att) {
      Object* join = new Object(remQuotes(vicGame->safeGetString("start_date", "\"1836.1.1\"")));
      history->setValue(join);
      join->setLeaf("add_attacker", addQuotes((*att)->getKey()));
      vicWar->setLeaf("attacker", addQuotes((*att)->getKey()));
    }
    for (objiter def = vicDef.begin(); def != vicDef.end(); ++def) {
      Object* join = new Object(remQuotes(vicGame->safeGetString("start_date", "\"1836.1.1\"")));
      history->setValue(join);
      join->setLeaf("add_defender", addQuotes((*def)->getKey()));
      vicWar->setLeaf("defender", addQuotes((*def)->getKey()));
    }
    Object* wargoal = new Object("war_goal");
    vicWar->setValue(wargoal);
    wargoal->setLeaf("casus_belli", "\"humiliate\"");
    wargoal->setLeaf("actor", addQuotes(vicAtt[0]->getKey()));
    wargoal->setLeaf("receiver", addQuotes(vicDef[0]->getKey()));
    vicWar->setLeaf("action", vicGame->safeGetString("start_date", "\"1836.1.1\""));
  }


  map<Object*, map<Object*, bool> > done; 
  for (objiter vc1 = vicCountries.begin(); vc1 != vicCountries.end(); ++vc1) {
    objvec leaves = (*vc1)->getLeaves();
    for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
      if (!vicGame->safeGetObject((*leaf)->getKey())) continue;
      if ((*leaf)->safeGetString("value", "NOVALUE") == "NOVALUE") continue;
      (*vc1)->removeObject(*leaf);
    }
    Object* eu1 = findEu3CountryByVicCountry(*vc1);
    if (!eu1) continue;
   
    objiter vc2 = vc1;
    for (++vc2; vc2 != vicCountries.end(); ++vc2) {
      Object* eu2 = findEu3CountryByVicCountry(*vc2);
      if (!eu2) continue;
      if (eu2 == eu1) continue;
      if (done[*vc1][*vc2]) continue;
      done[*vc1][*vc2] = true;
      done[*vc2][*vc1] = true;

      Object* eu3Relation1 = eu1->safeGetObject(eu2->getKey());
      if (!eu3Relation1) continue;
      Object* eu3Relation2 = eu2->safeGetObject(eu1->getKey());
      if (!eu3Relation2) continue;
      string value = eu3Relation1->safeGetString("value", "0.000"); 
      
      Object* relationForVic1 = new Object((*vc2)->getKey());
      relationForVic1->setLeaf("value", value);
      if (eu3Relation1->safeGetString("military_access", "no") == "yes") relationForVic1->setLeaf("military_access", "yes");
      (*vc1)->setValue(relationForVic1);
      Object* relationForVic2 = new Object((*vc1)->getKey());
      relationForVic2->setLeaf("value", value);
      if (eu3Relation2->safeGetString("military_access", "no") == "yes") relationForVic2->setLeaf("military_access", "yes");
      (*vc2)->setValue(relationForVic2);
    }
  }

  objvec spheres = edip->getValue("sphere");
  for (objiter v = vassals.begin(); v != vassals.end(); ++v) {
    Object* boss = findVicCountryByEu3Country(eu3Game->safeGetObject(remQuotes((*v)->safeGetString("first"))));
    if (!boss) continue;
    if (boss->safeGetString("greatPower", "no") != "yes") continue; 
    Object* underling = findVicCountryByEu3Country(eu3Game->safeGetObject(remQuotes((*v)->safeGetString("second"))));
    if (!underling) continue;

    Object* relation = boss->safeGetObject(underling->getKey());
    if (!relation) continue;
    relation->resetLeaf("level", "5"); 
  }
  
}

Object* extractPosition (Object* dat) {
  Object* ret = dat->safeGetObject("city");
  if (ret) return ret;
  ret = dat->safeGetObject("unit");
  if (ret) return ret;
  ret = dat->safeGetObject("port");
  if (ret) return ret;
  ret = dat->safeGetObject("manufactory");
  if (ret) return ret;
  ret = dat->safeGetObject("text_position");
  if (ret) return ret;
  return 0; 
}

void fillHistory (ProvinceHistory* currHistory,
		  int nDays,
		  string currOwner,
		  string currController,
		  string currReligion, 
		  string currCulture,
		  set<string> currCores, 
		  map<string, bool>& hasBuildings,
		  bool war,
		  string oRel,
		  string oGov,
		  double size,
		  double tax,
		  double mp) {

  currHistory->numDays = nDays; 
  currHistory->ownerTag = remQuotes(currOwner);
  currHistory->controllerTag = remQuotes(currController);
  currHistory->religion = currReligion;
  currHistory->culture = currCulture; 
  for (map<string, bool>::iterator b = hasBuildings.begin(); b != hasBuildings.end(); ++b) {
    currHistory->buildings[(*b).first] = (*b).second; 
  }
  currHistory->cores = currCores;
  currHistory->ownerAtWar = war;
  currHistory->ownerReligion = oRel;
  currHistory->ownerGovernment = oGov;
  currHistory->citysize = size;
  currHistory->basetax = tax;
  currHistory->manpower = mp; 
}

bool WorkerThread::hasBuildingOrBetter (string building, Object* dat) {
  if (dat->safeGetString(building, "no") == "yes") return true;
  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
    if (dat->safeGetString((*b)->getKey(), "no") == "no") continue;
    string precursor = (*b)->safeGetString("previous", "NONE");
    while (precursor != "NONE") {
      //Logger::logStream(Logger::Debug) << "Recursing on " << precursor << "\n"; 
      if (precursor == building) return true;
      for (objiter p = buildingTypes.begin(); p != buildingTypes.end(); ++p) {
	if (precursor != (*p)->getKey()) continue;
	precursor = (*p)->safeGetString("previous", "NONE");
	break; 
      }
    }
  }
  return false;
}

bool WorkerThread::countrySatisfied (Object* ownerModifier, ProvinceHistory* period, int numDays) {
  if ((ownerModifier->safeGetString("war", "no") == "yes") && (period->ownerAtWar)) {
    //Logger::logStream(Logger::Game) << "Found a war.\n"; 
    return true;
  }
  if ((ownerModifier->safeGetString("government", "NOEFFINGGOV") == period->ownerGovernment)) return true; 
  
  for (vector<string>::iterator s = sliders.begin(); s != sliders.end(); ++s) {
    int required = ownerModifier->safeGetInt((*s), -100);
    if (-100 == required) continue;
    Object* owner = findEu3CountryByTag(period->ownerTag);
    if (!owner) continue;
    int final = owner->safeGetInt((*s), 0);
    double atTheTime = 0;
    if (-1 == numDays) atTheTime = final;
    else {
      Object* ownerHistory = owner->safeGetObject("history");
      if (!ownerHistory) continue;
      int initial = ownerHistory->safeGetInt((*s), 0);
      atTheTime = initial + (initial-1.0*final)*(numDays-gameStartDays)/(gameDays-gameStartDays);
    }
    if (atTheTime >= required) return true;
  }
   
  // Stuff that can only be done in snapshots
  if (-1 == numDays) {
    Object* owner = findEu3CountryByTag(period->ownerTag);
    if (owner) {
      int reqStab = ownerModifier->safeGetInt("stability", -999);
      if (reqStab != -999) {
	int actualStab = (int) floor(owner->safeGetFloat("stability", 0) + 0.5);
	return (actualStab >= reqStab);
      }
    }

    int reqInfantry = ownerModifier->safeGetInt("infantry", -1);
    if (-1 < reqInfantry) return owner->safeGetInt("infRegiments") >= reqInfantry;
    int reqArtillery = ownerModifier->safeGetInt("artillery", -1);
    if (-1 < reqArtillery) return owner->safeGetInt("artRegiments") >= reqArtillery;

    objvec techGroups = ownerModifier->getValue("technology_group");
    string actualGroup = owner->safeGetString("technology_group");
    for (objiter tg = techGroups.begin(); tg != techGroups.end(); ++tg) {
      if (actualGroup == (*tg)->getLeaf()) return true; 
    }

    double reqPrestige = ownerModifier->safeGetFloat("prestige", -100);
    if (-1 < reqPrestige) {
      reqPrestige *= 100; // Stored differently in save
      if (owner->safeGetFloat("precise_prestige") >= reqPrestige) return true;
    }

    double reqTrade = ownerModifier->safeGetFloat("trade_income_percentage", -1);
    if (0 < reqTrade) {
      return (owner->safeGetFloat("tradePercentage") > reqTrade);
    }

    double reqGold = ownerModifier->safeGetFloat("gold_income_percentage", -1);
    if (0 < reqGold) {
      return (owner->safeGetFloat("goldPercentage") > reqGold);
    }

    int reqPorts = ownerModifier->safeGetInt("num_of_ports", -100);
    if (0 < reqPorts) {
      return (owner->safeGetInt("num_ports") >= reqPorts);
    }

    int reqEmbargos = ownerModifier->safeGetInt("num_of_trade_embargos", -100);
    if (0 < reqEmbargos) {
      return (owner->safeGetInt("numEmbargos") >= reqEmbargos); 
    }
    
    // Missing: Tech
    // Missing: Country modifier
  }
  return false; 
}

bool WorkerThread::isSatisfied (Object* mod, ProvinceHistory* period, Object* eu3prov, int numDays) {

  bool debugThis = (mod->safeGetString("debug", "no") == "yes");
  //static bool notPrinted = true;
  if (debugThis) Logger::logStream(Logger::Debug) << "Debugging "
						  << mod
						  << "\n";
  /*  
  if ((debugThis) && (period->ownerAtWar) && (notPrinted)) {
    notPrinted = false; 
    Logger::logStream(PopDebug) << "Found a war.\n";
  }
  */
  if (!period) period = eu3ProvinceToHistoryMap[eu3prov].back();       
 
  string building = mod->safeGetString("has_building", "NONE");
  if ((building != "NONE") && (period->buildings[building])) return true;
  objvec possibleGoods = mod->getValue("trade_goods");
  for (objiter pg = possibleGoods.begin(); pg != possibleGoods.end(); ++pg) {
    if (eu3prov->safeGetString("trade_goods", "NODANGGOODS") != (*pg)->getLeaf()) continue;
    if (debugThis) Logger::logStream(Logger::Debug) << "Trade good " << (*pg)->getLeaf() << " modifier found.\n"; 
    return true; 
  }

  // Missing: Continent 
  
  if (period->ownerReligion == mod->safeGetString("religion", "NODANGRELIGION")) return true;
  if ((mod->safeGetString("port", "no") == "yes") && (heuristicIsCoastal(eu3prov))) return true;
  string relGroup = mod->safeGetString("religion_group", "NONE");
  if ((relGroup != "NONE") && (religionMap[period->religion]) && (religionMap[period->religion]->safeGetString("group") == relGroup)) return true; 
  if ((mod->safeGetString("controlled_by", "BLAH") == "owner") && (period->controllerTag == period->ownerTag)) return true;  

  if (mod->safeGetString("is_overseas", "no") == "yes") {
    if (isOverseasEu3Provinces(period->capital, eu3prov)) return true;
  }

  Object* ownerModifier = mod->safeGetObject("owner");   
  if (ownerModifier) {
    Object* negate = ownerModifier->safeGetObject("NOT");
    if (negate) return !countrySatisfied(negate, period, numDays);
    return countrySatisfied(ownerModifier, period, numDays);
  }


  if (-1 != numDays) return false; 
  
  // Snapshot non-owner things
  int reqUnits = mod->safeGetInt("units_in_province", -1);
  if (reqUnits > -1) {
    int actualUnits = eu3prov->safeGetInt("regiments");
    if (debugThis) Logger::logStream(Logger::Debug) << "Found " << actualUnits << " units, needed " << reqUnits << "\n"; 
    if (actualUnits >= reqUnits) return true; 
  }
  

  
  return false; 
}

double WorkerThread::demandModifier (Object* mod, ProvinceHistory* period, Object* eu3prov, int numDays) {
  double mult = mod->safeGetFloat("factor", 1);
  if (isSatisfied(mod, period, eu3prov, numDays)) return mult; 
  
  Object* negate = mod->safeGetObject("NOT");
  if ((negate) && (!isSatisfied(negate, period, eu3prov, numDays))) return mult; 

  Object* many = mod->safeGetObject("OR");
  if ((many) && (isSatisfied(many, period, eu3prov, numDays))) return mult;
  return 1; 
}

int interpolate (int currDays, int totalDays, int iVal, int fVal, double exponent) {
  if (totalDays == 0) return iVal;
  double currValue = (fVal - iVal);

  currValue *= pow((1.0* currDays) / totalDays, exponent); 
  currValue += iVal; 

  return (int) floor(currValue + 0.5); 
}

void WorkerThread::interpolateInHistory (objvec& history, string keyword, int initialValue, int finalValue, double exponent) {
  int initialDays = gameStartDays;
  
  objiter lastNotUpdated = history.begin(); 
  for (objiter event = history.begin(); event != history.end(); ++event) {
    int currDays = days((*event)->getKey());
    if (currDays < initialDays) continue; 
    int eventValue = (*event)->safeGetInt(keyword);
    if (0 == eventValue) continue; 
    for (; (*lastNotUpdated) != (*event); ++lastNotUpdated) {
      int evtDays = days((*lastNotUpdated)->getKey());
      if (evtDays < initialDays) continue;
      (*lastNotUpdated)->resetLeaf(keyword, interpolate(evtDays - initialDays, currDays - initialDays, initialValue, eventValue, exponent));
    }
    initialDays = currDays; 
    initialValue = eventValue; 
  }
  
  //if (finalValue < initialValue) finalValue = initialValue; 
  
  for (; lastNotUpdated != history.end(); ++lastNotUpdated) {
    int evtDays = days((*lastNotUpdated)->getKey());
    if (evtDays < initialDays) continue;
    (*lastNotUpdated)->resetLeaf(keyword, interpolate(evtDays - initialDays, gameDays - initialDays, initialValue, finalValue, exponent));
  }
}

Object* getEu3ProvinceByTag (Object* eu3Game, string tag) {
  static map<string, Object*> provmap;
  if (provmap[tag]) return provmap[tag];
  provmap[tag] = eu3Game->safeGetObject(tag);
  return provmap[tag]; 
}

void WorkerThread::initialise () {
  vicGame->unsetValue("rebel_faction"); 
  objvec allVicObjects = vicGame->getLeaves();
  for (objiter vo = allVicObjects.begin(); vo != allVicObjects.end(); ++vo) {
    objvec armies = (*vo)->getValue("army");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regs = (*army)->getValue("regiment");
      for (objiter reg = regs.begin(); reg != regs.end(); ++reg) {
	regimentRatio[(*reg)->safeGetString("type")]++; 
      }
    }
    objvec navies = (*vo)->getValue("navy");
    for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
      objvec regs = (*navy)->getValue("ship");
      for (objiter reg = regs.begin(); reg != regs.end(); ++reg) {
	regimentRatio[(*reg)->safeGetString("type")]++; 
      }
    }
    
    (*vo)->unsetValue("leader");
    (*vo)->unsetValue("army");
    (*vo)->unsetValue("navy");
    (*vo)->unsetValue("primary_culture");
    (*vo)->unsetValue("culture");
    (*vo)->unsetValue("religion"); 
  }
  
  string gamedate = remQuotes(eu3Game->safeGetString("date"));
  string startdate = remQuotes(eu3Game->safeGetString("start_date"));
  gameDays = days(gamedate);
  gameStartDays = days(startdate);
  Logger::logStream(Logger::Debug) << "Start and end dates: "
    				   << startdate << " (" << gameStartDays << ") "
				   << gamedate << " (" << gameDays << ") "
				   << (gameDays - gameStartDays) << ".\n";
  
  objvec allEu3Objects = eu3Game->getLeaves();
  for (objiter g = allEu3Objects.begin(); g != allEu3Objects.end(); ++g) {
    if (0 == atoi((*g)->getKey().c_str())) continue;
    if ((*g)->safeGetString("owner", "NONE") == "NONE") continue; 
    eu3TagToSizeMap[remQuotes((*g)->safeGetString("owner", "NONE"))]++;
    eu3Provinces.push_back(*g); 
  }

  sliders.push_back("aristocracy_plutocracy");
  sliders.push_back("centralization_decentralization");
  sliders.push_back("innovative_narrowminded");
  sliders.push_back("mercantilism_freetrade");
  sliders.push_back("offensive_defensive");
  sliders.push_back("land_naval");
  sliders.push_back("quality_quantity");
  sliders.push_back("serfdom_freesubjects");

  objvec wars = eu3Game->getValue("previous_war");
  objvec currwars = eu3Game->getValue("active_war");
  for (objiter w = currwars.begin(); w != currwars.end(); ++w) wars.push_back(*w);

  static DateAscendingSorter dateSorter; 
  
  // Look for triggered modifiers.
  for (map<string, Object*>::iterator t = trigMods.begin(); t != trigMods.end(); ++t) {
    Object* trig = (*t).second->safeGetObject("trigger");
    if (!trig) continue;
    objvec provs = trig->getValue("owns");
    if (0 == provs.size()) continue;
    objvec trigProvinceEvents;
    for (objiter p = provs.begin(); p != provs.end(); ++p) {
      Object* eu3prov = eu3Game->safeGetObject((*p)->getLeaf());
      if (!eu3prov) {
	Logger::logStream(Logger::Warning) << "Warning: Could not find EU3 province "
					   << (*p)->getLeaf()
					   << ", needed for triggered modifier "
					   << (*t).first
					   << ".\n"; 
	continue;
      }
      Object* history = eu3prov->safeGetObject("history");
      Object* initial = new Object(startdate);
      initial->setLeaf("owner", remQuotes(history->safeGetString("owner", "\"NONE\"")));
      initial->setLeaf("province", eu3prov->getKey());
      trigProvinceEvents.push_back(initial);
      objvec evts = history->getLeaves();
      for (objiter evt = evts.begin(); evt != evts.end(); ++evt) {
	if (days((*evt)->getKey()) < 0) continue;
	string curr = remQuotes((*evt)->safeGetString("owner", "\"NONE\""));
	if (curr == "NONE") continue;
	initial = new Object((*evt)->getKey());
	initial->setLeaf("owner", curr);
	initial->setLeaf("province", eu3prov->getKey());
	trigProvinceEvents.push_back(initial);
      }
    }
    sort(trigProvinceEvents.begin(), trigProvinceEvents.end(), dateSorter);
    Object* allOwner = 0; 
    map<string, string> ownerships; 
    for (objiter tEvt = trigProvinceEvents.begin(); tEvt != trigProvinceEvents.end(); ++tEvt) {
      ownerships[(*tEvt)->safeGetString("province")] = (*tEvt)->safeGetString("owner");
      if (ownerships.size() < provs.size()) continue;
      string cand = "NONE"; 
      for (map<string, string>::iterator i = ownerships.begin(); i != ownerships.end(); ++i) {
	if (cand == "NONE") {cand = (*i).second; continue;}
	if (cand == (*i).second) continue;
	cand = "CONFLICT";
	break; 
      }
      bool allOwned = ((cand != "CONFLICT") && (cand != "NONE"));

      if (allOwned) {
	if (allOwner) continue; // No change
	allOwner = eu3Game->safeGetObject(cand);
	if (!allOwner) {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find candidate "
					     << cand
					     << " for triggered modifier "
					     << (*t).first
					     << ".\n";
	  continue; 
	}
	Object* history = allOwner->safeGetObject("history");
	Object* eligible = new Object((*tEvt)->getKey());
	eligible->setLeaf("gain_trig_mod_candidate", (*t).first);
	history->setValue(eligible); 
      }
      else {
	if (!allOwner) continue; // No change.
	Object* history = allOwner->safeGetObject("history");
	Object* eligible = new Object((*tEvt)->getKey());
	eligible->setLeaf("lose_trig_mod_candidate", (*t).first); 
	history->setValue(eligible);
	allOwner = 0; 
      }
    }
  }
  
  
  Logger::logStream(Logger::Game) << "Starting national-history analysis.\n"; 
  for (objiter g = allEu3Objects.begin(); g != allEu3Objects.end(); ++g) {
    if ((*g)->safeGetString("treasury", "NONE") == "NONE") continue;
    if ((*g)->safeGetString("stability", "NONE") == "NONE") continue;
    Object* history = (*g)->safeGetObject("history");
    if (!history) continue;

    // Search for inconsistency: Religion changes that don't match the final religion. 
    string historicReligion = history->safeGetString("religion"); 
    string currReligion = (*g)->safeGetString("religion");
    objvec events = history->getLeaves();
    for (objiter event = events.begin(); event != events.end(); ++event) {
      if ((*event)->safeGetString("religion", "BAH") == "BAH") continue;
      historicReligion = (*event)->safeGetString("religion"); 
    }
    if (historicReligion != currReligion) {
      // Look for new creations. Assume the religion changed at that time.
      bool foundRelease = false;
      int prevDays = gameStartDays; 
      for (objiter event = events.begin(); event != events.end(); ++event) {
	int indicators = 0; 
	if ((*event)->safeGetString("capital", "NOCAP") != "NOCAP") indicators++; 
	if ((*event)->safeGetObject("monarch")) indicators++;
	if ((*event)->safeGetString("government", "NOGOV") != "NOGOV") indicators++;
	if (days((*event)->getKey()) > prevDays + 3650*2) indicators++; 
	prevDays = days((*event)->getKey()); 
	if (indicators < 2) continue; 
	(*event)->resetLeaf("religion", currReligion);
	foundRelease = true; 
	break; 
      }

      if (!foundRelease) {
	Logger::logStream(Logger::Warning) << "Warning: Tag "
					   << (*g)->getKey()
					   << " has historic religion "
					   << historicReligion
					   << " but current religion "
					   << currReligion
					   << ". Resetting history start.\n";
	history->resetLeaf("religion", currReligion);
      } 
    }

    // Insert manual events.
    Object* extras = configObject->safeGetObject("extraEvents");
    if (extras) {
      Object* currExtra = extras->safeGetObject((*g)->getKey());
      if (currExtra) {
	objvec currExtraEvents = currExtra->getLeaves();
	for (objiter cee = currExtraEvents.begin(); cee != currExtraEvents.end(); ++cee) {
	  history->setValue(*cee); 
	}
      }
    }

    
    // Heuristic tech calculation. 
    int initialProdTech = 3;
    int finalProdTech   = 3;    
    Object* finalTechObject = (*g)->safeGetObject("technology");
    if (finalTechObject) {
      Object* prTech = finalTechObject->safeGetObject("production_tech");
      finalProdTech = atoi(prTech->getToken(0).c_str()); 
    }
    interpolateInHistory(events, "production_tech", initialProdTech, finalProdTech, 1);
    // Done with tech calculation. 

    map<string, int> inialSliders;
    map<string, int> finalSliders; 
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      inialSliders[*i] = history->safeGetInt((*i), 5); 
      finalSliders[*i] = (*g)->safeGetInt((*i), 0); 
      interpolateInHistory(events, (*i), inialSliders[*i], finalSliders[*i], 0.5);
    }
    
    string currTag = (*g)->getKey(); 
    for (objiter war = wars.begin(); war != wars.end(); ++war) {
      Object* warHistory = (*war)->safeGetObject("history");
      if (!warHistory) continue;
      objvec warevents = warHistory->getLeaves();
      for (objiter ev = warevents.begin(); ev != warevents.end(); ++ev) {
	if ((currTag == remQuotes((*ev)->safeGetString("add_attacker"))) ||
	    (currTag == remQuotes((*ev)->safeGetString("add_defender")))) {
	  (*ev)->resetLeaf("addwar", "yes");
	  history->setValue(*ev);
	  continue;
	}
	if ((currTag == remQuotes((*ev)->safeGetString("rem_attacker"))) ||
	    (currTag == remQuotes((*ev)->safeGetString("rem_defender")))) {
	  (*ev)->resetLeaf("remwar", "yes");
	  history->setValue(*ev);
	}
      }
    }

    int currProdTech = 3;
    map<string, int> currSliders; 
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      currSliders[*i] = inialSliders[*i];
    } 
    string currGovernment = history->safeGetString("government", "NONE");
    set<string> currModNames; 
    Object* currCapital = eu3Game->safeGetObject(history->safeGetString("capital", "noprovince"));
    int numWars = 0;
   
    events = history->getLeaves();
    stable_sort(events.begin(), events.end(), dateSorter); 
    int prevDays = gameStartDays;

    // Insert special event for nations with empty histories. 
    bool hasEvent = false;
    for (objiter event = events.begin(); event != events.end(); ++event) {
      int currDays = days((*event)->getKey());
      if (0 > currDays) continue;
      hasEvent = true;
      break;
    }
    if (!hasEvent) { 
      Object* specialEvent = new Object(startdate);
      specialEvent->setLeaf("special", "yes");
      events.push_back(specialEvent); 
    }

    
    for (objiter event = events.begin(); event != events.end(); ++event) {
      int currDays = days((*event)->getKey());
      if (0 > currDays) continue;

      Object* eventMod     = 0;
      Object* eventRemMod  = 0;
      string eventDec = remQuotes((*event)->safeGetString("decision", "\"nodecision\""));
      if (nameToDecisionMap[eventDec]) {
	eventMod    = nameToModifierMap[nameToDecisionMap[eventDec]->safeGetString("name")];
	eventRemMod = nameToModifierMap[nameToDecisionMap[eventDec]->safeGetString("remove_country_modifier")];
	currModNames.insert(nameToDecisionMap[eventDec]->safeGetString("name"));
	currModNames.erase(nameToDecisionMap[eventDec]->safeGetString("remove_country_modifier")); 
      }

      Object* leader = (*event)->safeGetObject("leader");
      if (leader) {
	Object* id = leader->safeGetObject("id");
	leaderMap[id->safeGetString("id")] = leader; 
      }
      
      map<string, int> eventSliders;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	eventSliders[*i]   = (*event)->safeGetInt((*i), -99); 
      }
      int eventProdTech    = (*event)->safeGetInt("production_tech"); 
      string eventGov = (*event)->safeGetString("government", "NONE");
      string eventRel = (*event)->safeGetString("religion", "NONE");
      Object*     eventCap = eu3Game->safeGetObject((*event)->safeGetString("capital", "noprovince"));
      int eventWar = 0; 
      if ((*event)->safeGetString("addwar", "no") == "yes") eventWar = 1;
      if ((*event)->safeGetString("remwar", "no") == "yes") eventWar = -1;
      
      bool important = false;
      if ((eventProdTech != 0) && (eventProdTech > currProdTech)) important = true; 
      if (eventGov != "NONE") important = true;
      if (eventRel != "NONE") important = true;
      if ((0 == numWars) && (1 == eventWar)) important = true;
      if ((1 == numWars) && (-1 == eventWar)) important = true;
      if ((eventCap) && (currCapital != eventCap)) important = true; 
      numWars += eventWar;
      if (numWars < 0) numWars = 0;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	if ((-99 != eventSliders[*i]) && (eventSliders[*i] != currSliders[*i])) important = true;
      }
      if (eventMod) important = true;
      if (eventRemMod) important = true; 
      
      (*event)->resetLeaf("numWars", numWars);
      (*event)->resetLeaf("nationalGovernment", eventGov == "NONE" ? currGovernment : eventGov);
      if ((*event)->safeGetString("nationalGovernment", "NONE") == "") debugObject->safeGetString("blah");
      (*event)->resetLeaf("nationalReligion", eventRel == "NONE" ? currReligion : eventRel); 
      (*event)->resetLeaf("capital", eventCap ? eventCap->getKey() : currCapital ? currCapital->getKey() : "nocapital"); 
      for (set<string>::iterator m = currModNames.begin(); m != currModNames.end(); ++m) {
	(*event)->setLeaf("modifier", (*m));
      }
      
      if (!important) continue;

      NationHistory* currHistory = new NationHistory();
      currHistory->religion = currReligion;
      currHistory->government = currGovernment;
      currHistory->numDays = (currDays - prevDays);
      currHistory->numWars = numWars;
      currHistory->capital = currCapital;
      currHistory->productionTech = currProdTech;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	currHistory->sliders[*i] = currSliders[*i];
      } 
      if (eventRemMod) {
	objiter remPosition = find(currHistory->modifiers.begin(), currHistory->modifiers.end(), eventRemMod);
	if (remPosition != currHistory->modifiers.end()) currHistory->modifiers.erase(remPosition);
      }
      if (eventMod) currHistory->modifiers.push_back(eventMod);

      eu3NationToHistoryMap[*g].push_back(currHistory);
      prevDays = currDays;
      if (eventGov != "NONE")  currGovernment = eventGov;
      if (eventRel != "NONE")  currReligion   = eventRel;
      if (eventCap)            currCapital    = eventCap;
      if (eventProdTech != -1) currProdTech   = eventProdTech;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	if ((-99 != eventSliders[*i]) && (eventSliders[*i] != currSliders[*i])) currSliders[*i] = eventSliders[*i];
      }
    }

    string finalGov = (*g)->safeGetString("government", "NONE");
    string finalRel = (*g)->safeGetString("religion", "NONE");
    
    NationHistory* currHistory = new NationHistory();
    currHistory->religion   = finalRel;
    currHistory->government = finalGov;
    currHistory->numDays    = (gameDays - prevDays);
    currHistory->numWars    = numWars;
    currHistory->capital    = eu3Game->safeGetObject((*g)->safeGetString("capital"));
    currHistory->productionTech   = finalProdTech;
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      currHistory->sliders[*i] = finalSliders[*i];
    }

    objvec finalMods = (*g)->getValue("modifier");
    for (objiter fmod = finalMods.begin(); fmod != finalMods.end(); ++fmod) {
      Object* mod = nameToModifierMap[remQuotes((*fmod)->safeGetString("modifier", "\"nomod\""))];
      if (mod) currHistory->modifiers.push_back(mod); 
    }
    
    eu3NationToHistoryMap[*g].push_back(currHistory);
  }

  Logger::logStream(Logger::Game) << "Done with national histories. Starting province histories. (This will take several minutes.) \n"; 

  double prevtime = GetTickCount();
  double currtime = 0;
  double times[10]; 
  for (int i = 0; i < 10; ++i) times[i] = 0; 

#ifdef DEBUG
#define TIMER(n) currtime = GetTickCount(); times[n] += (currtime - prevtime); prevtime = currtime; 
#else
#define TIMER(n)
#endif
  
  for (objiter eu3prov = eu3Provinces.begin(); eu3prov != eu3Provinces.end(); ++eu3prov) {
    Object* hist = (*eu3prov)->safeGetObject("history");
    if (!hist) continue;
    string currOwnerTag = (*eu3prov)->safeGetString("owner", "NONE");
    if (currOwnerTag == "NONE") continue;

    //Logger::logStream(Logger::Debug) << (*eu3prov)->getKey() << "\n"; 

    bool debugProvince = (*eu3prov)->safeGetString("debugPop", "no") == "yes";

    // Look for inconsistent history: Owner, culture or religion in history doesn't match final.
    string currReligion   = (*eu3prov)->safeGetString("religion", "NONE");
    string currCulture    = (*eu3prov)->safeGetString("culture", "NONE");    
    string histOwnerTag   = hist->safeGetString("owner", "NONE");
    string histReligion   = hist->safeGetString("religion", "NONE");
    string histCulture    = hist->safeGetString("culture", "NONE");    
    objvec evts = hist->getLeaves();
    for (objiter event = evts.begin(); event != evts.end(); ++event) {
      histReligion = (*event)->safeGetString("religion", histReligion);
      histCulture  = (*event)->safeGetString("culture", histCulture);      
      histOwnerTag = (*event)->safeGetString("owner", histOwnerTag);
    }
    if (histReligion != currReligion) {
      Logger::logStream(Logger::Warning) << "Warning: Province "
					 << nameAndNumber(*eu3prov) 
					 << " has historic religion "
					 << histReligion
					 << " but current religion "
					 << currReligion
					 << ". Resetting history start.\n";
      hist->resetLeaf("religion", currReligion);
    }
    if (histCulture != currCulture) {
      Logger::logStream(Logger::Warning) << "Warning: Province "
					 << nameAndNumber(*eu3prov) 
					 << " has historic culture "
					 << histCulture
					 << " but current culture "
					 << currCulture
					 << ". Resetting history start.\n";
      hist->resetLeaf("culture", currCulture);
    }    
    if (histOwnerTag != currOwnerTag) {
      Logger::logStream(Logger::Warning) << "Warning: Province "
					 << nameAndNumber(*eu3prov) 
					 << " has historic owner "
					 << histOwnerTag 
					 << " but current owner "
					 << currOwnerTag
					 << ". Suggest by-hand fix.\n"; 
    }
    
    // Insert national histories
    currOwnerTag      = hist->safeGetString("owner", "NONE");
    Object* currOwner = findEu3CountryByTag(currOwnerTag);
    objvec events = hist->getLeaves();
    objvec allEvents;
    int ownerChange = gameStartDays;
    if (0 == atoi((*eu3prov)->getKey().c_str()) % 10) {
#ifdef DEBUG
      Logger::logStream(Logger::Game) << "Province "
				      << nameAndNumber(*eu3prov) << " "
				      << times[0] << " " << times[1] << " " << times[2] << " " << times[3] << " " << times[4] << " "
				      << times[5] << " " << times[6] << " " << times[7] << " " << times[8] << " " << times[9] << " " 
				      << "\n";
#else
      currtime = GetTickCount();      
      Logger::logStream(Logger::Game) << "Province "
				      << nameAndNumber(*eu3prov) << " "
				      << (currtime - prevtime)*0.001
				      << "\n";
      prevtime = currtime;
#endif
    }
    if (debugProvince) Logger::logStream(Logger::Debug) << "Starting debug history run on "
							<< nameAndNumber(*eu3prov)
							<< ". Initial owner "
							<< currOwnerTag 
							<< "\n";
    double provinceDays = 0;
    double warDays = 0;
    TIMER(0);
    
    for (objiter event = events.begin(); event != events.end(); ++event) {
      int eventDays = days((*event)->getKey());
      if (0 > eventDays) continue;
      //if (gameStartDays > eventDays) eventDays = gameStartDays; 
      // Copying province events
      allEvents.push_back(*event);
      
      if ((*event)->safeGetString("owner", "NOCHANGE") == "NOCHANGE") continue;

      // Copying national events up to this ownership change 
      if (currOwner) {
	Object* nationHistory = currOwner->safeGetObject("history");
	objvec natEvents = nationHistory->getLeaves();
	for (objiter n = natEvents.begin(); n != natEvents.end(); ++n) {
	  int nDays = days((*n)->getKey()); 
	  if (0 > nDays) continue;
	  if (ownerChange > nDays) continue;
	  if (eventDays < nDays) {
	    if (debugProvince) Logger::logStream(Logger::Debug) << currOwner->getKey() << " : Skipping event " << (*n) << " due to eventDays.\n"; 
	    break;
	  }
	  allEvents.push_back(*n); 
	}
      }

      TIMER(1);
      currOwnerTag = (*event)->safeGetString("owner", "NONE");
      currOwner    = findEu3CountryByTag(currOwnerTag);
      if (!currOwner) continue; 
      ownerChange  = eventDays;

      // Inserting national info for the new-owner event immediately prior to this ownership change.
      Object* nationHistory = currOwner->safeGetObject("history");
      objvec natEvents = nationHistory->getLeaves();
      for (objvec::reverse_iterator n = natEvents.rbegin(); n != natEvents.rend(); ++n) {
	int nDays = days((*n)->getKey());
	if (0 > nDays) continue; 
	if (ownerChange < nDays) continue;
	(*event)->resetLeaf("numWars", (*n)->safeGetInt("numWars"));
	(*event)->resetLeaf("nationalGovernment", (*n)->safeGetString("nationalGovernment"));
	if ((*event)->safeGetString("nationalGovernment", "NONE") == "") debugObject->safeGetString("blah");
	(*event)->resetLeaf("nationalReligion", (*n)->safeGetString("nationalReligion"));
	(*event)->resetLeaf("capital", (*n)->safeGetString("capital"));
	(*event)->unsetValue("modifier"); 
	objvec mods = (*n)->getValue("modifier"); 
	for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
	  (*event)->setValue(*mod); 
	}
	if (debugProvince) Logger::logStream(Logger::Debug) << "Ownership change: New owner "
							    << currOwner->getKey()
							    << ", wars "
							    << (*n)->safeGetInt("numWars", -1)
							    << "\n"; 
	break; 
      }
    }

    // Insert national events for the final owner. 
    if (currOwner) {
      Object* nationHistory = currOwner->safeGetObject("history");
      objvec natEvents = nationHistory->getLeaves();
      for (objiter n = natEvents.begin(); n != natEvents.end(); ++n) {
	int nDays = days((*n)->getKey()); 
	if (0 > nDays) continue;
	if (ownerChange > nDays) continue;
	allEvents.push_back(*n); 
      }
    }
    // End adding national events. 
    TIMER(2);
    
    static DateAscendingSorter dateSorter;
    stable_sort(allEvents.begin(), allEvents.end(), dateSorter); 

    currOwnerTag = hist->safeGetString("owner", "NONE");
    currOwner    = findEu3CountryByTag(currOwnerTag);
    string currController      = hist->safeGetString("controller", "NONE");
    currReligion               = hist->safeGetString("religion",   "NONE");
    currCulture                = hist->safeGetString("culture",    "NONE");
    double currCitySize        = hist->safeGetFloat("citysize", 0);
    double currBaseTax         = hist->safeGetFloat("base_tax", 1);
    double currManpower        = hist->safeGetFloat("manpower", 1);
    map<string, bool> hasBuildings;
    for (objiter building = buildingTypes.begin(); building != buildingTypes.end(); ++building) {
      hasBuildings[(*building)->getKey()] = hasBuildingOrBetter((*building)->getKey(), hist);
    }
    set<string> currCores;
    objvec tempCores = hist->getValue("core");
    for (objiter c = tempCores.begin(); c != tempCores.end(); ++c) {
      currCores.insert(remQuotes((*c)->getLeaf()));
    }

    map<string, int> currSliders; 
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      currSliders[*i] = currOwner ? eu3NationToHistoryMap[currOwner][0]->sliders[*i] : 5;
    }
   
    int currProdTech           = currOwner ? eu3NationToHistoryMap[currOwner][0]->productionTech : 3; 
    string currOwnerReligion   = currOwner ? eu3NationToHistoryMap[currOwner][0]->religion : "NONE";
    string currOwnerGovernment = currOwner ? eu3NationToHistoryMap[currOwner][0]->government : "NONE";
    string currCap             = currOwner ? eu3NationToHistoryMap[currOwner][0]->capital->getKey() : "noCap";

    vector<string> currTrigModCandidates;    
    objvec currModifiers; 
    if (currOwner) {
      for (objiter i = eu3NationToHistoryMap[currOwner][0]->modifiers.begin(); i !=  eu3NationToHistoryMap[currOwner][0]->modifiers.end(); ++i) {
	currModifiers.push_back(*i); 
      }
    }
    TIMER(3);
    int previousEventDays = gameStartDays;
    int numWars = 0; 
    for (objiter event = allEvents.begin(); event != allEvents.end(); ++event) {
      int eventDays = days((*event)->getKey());
      if (0 > eventDays) continue;
      if (gameStartDays > eventDays) continue; 

      if (debugProvince) Logger::logStream(Logger::Debug) << "Province event: " << (*event) << "\n"; 

      map<string, int> eventSliders; 
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	eventSliders[*i] = (*event)->safeGetInt((*i), -99);
      }

      vector<string> eventGainTrigMod;
      vector<string> eventLoseTrigMod; 
      objvec tmods = (*event)->getValue("gain_trig_mod_candidate");
      for (objiter t = tmods.begin(); t != tmods.end(); ++t) eventGainTrigMod.push_back((*t)->getLeaf());
      tmods = (*event)->getValue("lose_trig_mod_candidate");
      for (objiter t = tmods.begin(); t != tmods.end(); ++t) eventLoseTrigMod.push_back((*t)->getLeaf());

      TIMER(4);      
      int eventProdTech      = (*event)->safeGetInt("production_tech");
      string eventOwner      = (*event)->safeGetString("owner",       "NONE");
      string eventController = (*event)->safeGetString("controller",  "NONE");
      string eventReligion   = (*event)->safeGetString("religion",    "NONE");
      string eventCulture    = (*event)->safeGetString("culture",     "NONE"); 
      string eventAddCore    = remQuotes((*event)->safeGetString("add_core", "NONE"));
      string eventRemCore    = remQuotes((*event)->safeGetString("remove_core", "NONE"));
      string eventNatGov     = (*event)->safeGetString("nationalGovernment", "NO.GOV.CHANGE");
      if ((*event)->safeGetString("nationalGovernment", "NONE") == "") debugObject->safeGetString("blah");
      string eventNatRel     = (*event)->safeGetString("nationalReligion", "NO.REL.CHANGE"); 
      double eventCitySize        = (*event)->safeGetFloat("citysize", -1);
      double eventBaseTax         = (*event)->safeGetFloat("base_tax", -1);
      double eventManpower        = (*event)->safeGetFloat("manpower", -1);
      int eventWars               = (*event)->safeGetInt("numWars", -1); 
      string eventCap        = (*event)->safeGetString("capital", "noCapChange");
      objvec eventModStrings      = (*event)->getValue("modifier");
      objvec eventModifiers;
      for (objiter str = eventModStrings.begin(); str != eventModStrings.end(); ++str) {
	Object* mod = nameToModifierMap[remQuotes((*str)->getLeaf())];
	if (mod) eventModifiers.push_back(mod); 
      }
      
      if ((eventOwner != "NONE") && (currController == "NONE")) eventController = eventOwner; 
      bool important = false;
      if (eventOwner      != "NONE")    important = true;
      if (eventController != "NONE")    important = true;
      if (eventReligion   != "NONE")    important = true;
      if (eventCulture    != "NONE")    important = true;
      if (eventAddCore    != "NONE")    important = true;
      if (eventRemCore    != "NONE")    important = true;
      if (0 <= eventCitySize)           important = true;
      if (0 <= eventBaseTax)            important = true;
      if (0 <= eventManpower)           important = true;
      if (eventProdTech > currProdTech) important = true; 
      if ((eventNatGov != "NO.GOV.CHANGE") && (eventNatGov != currOwnerGovernment)) important = true;
      if ((eventNatRel != "NO.REL.CHANGE") && (eventNatRel != currOwnerReligion)) important = true;
      if ((eventCap != "noCapChange") && (eventCap != currCap)) important = true; 
      for (objiter building = buildingTypes.begin(); building != buildingTypes.end(); ++building) {
	if ((*event)->safeGetString((*building)->getKey(), "ARGH") == "ARGH") continue;
	important = true;
	// If building something it is supposed to have, then it didn't in fact
	// have it until now, so don't put it in the stats for previous period.
	// Conversely, if removing something, then it had it, so put it in. 
	hasBuildings[(*building)->getKey()] = ((*event)->safeGetString((*building)->getKey()) == "no"); 
	break; 
      }
      if ((0 == numWars) && ( 0 != eventWars) && (-1 != numWars)) important = true;
      if ((0 != numWars) && ( 0 == eventWars) && (-1 != numWars)) important = true;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	if ((eventSliders[*i] != -99) && (eventSliders[*i] != currSliders[*i])) important = true;
      }

      if (0 < eventModifiers.size()) {
	if (eventModifiers.size() != currModifiers.size()) important = true;
	else {
	  // Same number of mods, so if all the event modifiers are found, the lists must be identical.
	  for (objiter evmod = eventModifiers.begin(); evmod != eventModifiers.end(); ++evmod) {
	    if (find(currModifiers.begin(), currModifiers.end(), (*evmod)) != currModifiers.end()) continue;
	    important = true;
	    break; 
	  }
	}
      }
      if ((eventGainTrigMod.size() != 0) || (eventLoseTrigMod.size() != 0)) important = true; 
      
      if (!important) continue; 
      
      ProvinceHistory* currHistory = new ProvinceHistory();
      fillHistory(currHistory, (eventDays - previousEventDays), currOwnerTag, currController, currReligion, currCulture,
		  currCores, hasBuildings, (numWars > 0), currOwnerReligion, currOwnerGovernment,
		  currCitySize, currBaseTax, currManpower);
      currHistory->productionTech = currProdTech;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	currHistory->sliders[*i] = currSliders[*i];
      }
      for (vector<string>::iterator tm = currTrigModCandidates.begin(); tm != currTrigModCandidates.end(); ++tm) {
	Object* trigMod = trigMods[*tm];
	if (!trigMod) continue;
	Object* trig = trigMod->safeGetObject("trigger");
	if (!trig) continue;
	Object* notslider = trig->safeGetObject("NOT");
	bool satisfy = true; 
	for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	  int atLeast  = trig->safeGetInt((*i), -99);
	  int lessThan = notslider ? notslider->safeGetInt((*i), -99) : -99;
	  int actual   = currHistory->sliders[*i];
	  if ((atLeast != -99) && (actual < atLeast)) satisfy = false;
	  if ((lessThan != -99) && (actual >= lessThan)) satisfy = false; 
	}
	if (!satisfy) continue; 
	currHistory->triggerMods.push_back(trigMod); 
      }
      for (vector<string>::iterator c = eventGainTrigMod.begin(); c != eventGainTrigMod.end(); ++c) {
	currTrigModCandidates.push_back(*c);
      }
      for (vector<string>::iterator c = eventLoseTrigMod.begin(); c != eventLoseTrigMod.end(); ++c) {
	vector<string>::iterator rem = find(currTrigModCandidates.begin(), currTrigModCandidates.end(), (*c));
	if (rem != currTrigModCandidates.end()) currTrigModCandidates.erase(rem); 
      }
      
      if ((debugProvince) && (currOwnerTag != currController)) {
	Logger::logStream(PopDebug) << "Adding "
				    << currHistory->numDays
				    << " occupation days at event "
				    << (*event)
				    << currOwnerTag << " "
				    << currController << " "
				    << currProdTech << " " 
				    << "\n"; 
      }
      for (objiter cmod = currModifiers.begin(); cmod != currModifiers.end(); ++cmod) {
	currHistory->modifiers.push_back(*cmod); 
      }
      TIMER(5);      
      //currHistory->capital = (currCap == "noCap" ? 0 : eu3Game->safeGetObject(currCap)); 
      currHistory->capital = (currCap == "noCap" ? 0 : getEu3ProvinceByTag(eu3Game, currCap)); 
      TIMER(6);                  
      currHistory->date = (*event)->getKey();
      TIMER(7);                  
      if (currHistory->ownerAtWar) {
	if (debugProvince) Logger::logStream(Logger::Debug) << "Adding " << currHistory->numDays
							    << " war days from event "
							    << (*event)
			     				    << currProdTech << " " 
							    << "\n"; 
	warDays += currHistory->numDays;
      }
      provinceDays += currHistory->numDays;
      TIMER(8);       
      eu3ProvinceToHistoryMap[*eu3prov].push_back(currHistory);

      TIMER(9);                  
      if (eventOwner != "NONE") {
	if (0 == currCores.count(eventOwner)) {
	  for (objiter building = buildingTypes.begin(); building != buildingTypes.end(); ++building) {
	    if ((*building)->safeGetString("destroy_on_conquest", "no") == "yes") hasBuildings[(*building)->getKey()] = false; 
	  }
	}
	if (remQuotes(eventOwner) == "---") eventOwner = "NONE"; 
	currOwnerTag = eventOwner;
	currOwner = findEu3CountryByTag(currOwnerTag);
      }
      if (eventAddCore    != "NONE") currCores.insert(eventAddCore);
      if (eventRemCore    != "NONE") currCores.erase(eventRemCore); 
      if (eventController != "NONE") currController = eventController;
      if (eventReligion   != "NONE") currReligion   = eventReligion;
      if (eventCulture    != "NONE") currCulture    = eventCulture;
      if (0 <= eventCitySize)        currCitySize   = eventCitySize;
      if (0 <= eventBaseTax)         currBaseTax    = eventBaseTax;
      if (0 <= eventManpower)        currManpower   = eventManpower;
      for (objiter building = buildingTypes.begin(); building != buildingTypes.end(); ++building) {
	if ((*event)->safeGetString((*building)->getKey(), "ARGH") != "ARGH")
	  hasBuildings[(*building)->getKey()] = ((*event)->safeGetString((*building)->getKey()) == "yes"); 
      }
      if (eventWars != -1) numWars = eventWars;
      if (debugProvince) Logger::logStream(Logger::Debug) << "Setting numWars to "
							  << numWars
							  << " due to event "
							  << (*event)
							  << "\n"; 
      previousEventDays = eventDays;
      if (eventNatGov != "NO.GOV.CHANGE") currOwnerGovernment = eventNatGov;
      if (eventNatRel != "NO.REL.CHANGE") currOwnerReligion   = eventNatRel;
      if (eventCap    != "noCapChange")   currCap = eventCap;
      if (eventProdTech > currProdTech)   currProdTech = eventProdTech;
      for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
	if ((eventSliders[*i] != -99) && (eventSliders[*i] != currSliders[*i])) currSliders[*i] = eventSliders[*i];
      }

      if (0 != eventModifiers.size()) {
	currModifiers.clear();
	for (objiter evmod = eventModifiers.begin(); evmod != eventModifiers.end(); ++evmod) {
	  currModifiers.push_back(*evmod); 
	}
      }
    }

    if (debugProvince) Logger::logStream(Logger::Debug) << "Ended event loop, setting final ProvinceHistory object.\n";
    
    currOwnerTag        = (*eu3prov)->safeGetString("owner",      "NONE");
    currController      = (*eu3prov)->safeGetString("controller", "NONE");
    currReligion        = (*eu3prov)->safeGetString("religion",   "NONE");
    currCulture         = (*eu3prov)->safeGetString("culture",    "NONE");
    currOwner           = findEu3CountryByTag(currOwnerTag);
    currOwnerReligion   = currOwner->safeGetString("religion");
    currOwnerGovernment = currOwner->safeGetString("government"); 
    currCap             = currOwner->safeGetString("capital");
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      currSliders[*i] = currOwner->safeGetInt(*i);
    }    
    int finalProdTech   = 3;    
    Object* finalTechObject = currOwner->safeGetObject("technology");
    if (finalTechObject) {
      Object* prTech = finalTechObject->safeGetObject("production_tech");
      finalProdTech = atoi(prTech->getToken(0).c_str()); 
    }
    if (finalProdTech < currProdTech) finalProdTech = currProdTech; 
    
    for (objiter building = buildingTypes.begin(); building != buildingTypes.end(); ++building) {
      //if (debugProvince) Logger::logStream(Logger::Debug) << "Checking for building " << (*building)->getKey() << "\n"; 
      if (hasBuildingOrBetter((*building)->getKey(), (*eu3prov))) continue;
      hasBuildings[(*building)->getKey()] = false; 
    }

    ProvinceHistory* currHistory = new ProvinceHistory();
    fillHistory(currHistory, (gameDays - previousEventDays), currOwnerTag, currController, currReligion, currCulture,
		currCores, hasBuildings, (0 < eu3NationToHistoryMap[currOwner].back()->numWars), currOwnerReligion, currOwnerGovernment,
		currCitySize, currBaseTax, currManpower);
    currHistory->productionTech = finalProdTech;
    for (vector<string>::iterator i = sliders.begin(); i != sliders.end(); ++i) {
      currHistory->sliders[*i] = currSliders[*i];
    }
    if (currHistory->ownerAtWar) warDays += currHistory->numDays;
    currHistory->capital = eu3Game->safeGetObject(currCap);
    provinceDays += currHistory->numDays;
    eu3ProvinceToHistoryMap[*eu3prov].push_back(currHistory);
    for (objiter i = eu3NationToHistoryMap[currOwner].back()->modifiers.begin(); i != eu3NationToHistoryMap[currOwner].back()->modifiers.end(); ++i) {
      currHistory->modifiers.push_back(*i); 
    }

    if (debugProvince) Logger::logStream(Logger::Debug) << "War days: " << warDays
							<< " / " << provinceDays
							<< " = " << (warDays / provinceDays)
							<< "\n"; 
  }
  
  static Object* defaultCity = new Object("defaultcity");

  double totalLiteracyWeight = 0; 
  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
    totalLiteracyWeight += (*b)->safeGetFloat("literacy"); 
  }
  
  for (objiter eu3prov1 = eu3Provinces.begin(); eu3prov1 != eu3Provinces.end(); ++eu3prov1) {
    Object* city1 = extractPosition(getEu3ProvInfoFromEu3Province(*eu3prov1));
    if (!city1) {
      Logger::logStream(Logger::Error) << "Error: No city information for EU3 province "
				       << nameAndNumber(*eu3prov1)
				       << ".\n";
      city1 = defaultCity; 
    }
    double x1 = city1->safeGetFloat("x", 0);
    double y1 = city1->safeGetFloat("y", 0);
    double totalInfluence = 0; 
    double maxPossible = 0; 
    double mapWidth = configObject->safeGetFloat("mapWidth", 5616);
    
    for (objiter eu3prov2 = eu3Provinces.begin(); eu3prov2 != eu3Provinces.end(); ++eu3prov2) {
      double distance = 1;
      Object* city2 = extractPosition(getEu3ProvInfoFromEu3Province(*eu3prov2)); 
      if (!city2) city2 = defaultCity;
      double x2 = city2->safeGetFloat("x", 1000);
      double y2 = city2->safeGetFloat("y", 1000);

      distance += min(sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2)), sqrt(pow(fabs(x2 - x1) - mapWidth, 2) + pow(y2 - y1, 2))); 
      double literacyWeight = (*eu3prov2)->safeGetFloat("literacyWeight", -1);
      if (0 > literacyWeight) {
	literacyWeight = 0;
	for (vector<ProvinceHistory*>::iterator i = eu3ProvinceToHistoryMap[*eu3prov2].begin(); i != eu3ProvinceToHistoryMap[*eu3prov2].end(); ++i) {
	  for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
	    if (!(*i)->buildings[(*b)->getKey()]) continue;
	    literacyWeight += (*i)->numDays * (*b)->safeGetFloat("literacy"); 
	  }
	}
	(*eu3prov2)->setLeaf("literacyWeight", literacyWeight);
	if (0 < literacyWeight) Logger::logStream(Logger::Debug) << "Province "
								 << nameAndNumber(*eu3prov2)
								 << " has literacy weight "
								 << literacyWeight
								 << ".\n";
      }

      totalInfluence += (literacyWeight / distance);
      maxPossible += totalLiteracyWeight*(gameDays - gameStartDays) / distance;
    }

    totalInfluence /= maxPossible;
    Logger::logStream(Logger::Debug) << "Province "
				     << nameAndNumber(*eu3prov1)
				     << " has literacy influence fraction "
				     << totalInfluence
				     << ".\n";
    
    (*eu3prov1)->resetLeaf("literacyInfluence", totalInfluence); 
  }

  Logger::logStream(Logger::Game) << "Starting trade prices calculation, this will take a while.\n"; 

  string calculation = configObject->safeGetString("calculatePrices", "no");
  for (objiter tradegood = goods.begin(); tradegood != goods.end(); ++tradegood) {
    string tradeKey = (*tradegood)->getKey(); 
    prices[tradeKey].resize(gameDays - gameStartDays);
    
    if (calculation == "readFromCache") {
      ifstream tradereader;
      string filename("./priceCache/");
      filename += tradeKey;
      tradereader.open(filename.c_str());
      double prev = 0;
      bool warned = false; 
      for (unsigned int i = 0; i < prices[tradeKey].size(); ++i) {
	double p = 0; 
	tradereader >> p;
	if (p > 0.0001) prev = p;
	else {
	  if (!warned) {
	    warned = true;
	    Logger::logStream(Logger::Debug) << "Warning: Ran out of prices for " << tradeKey << "\n"; 
	  }
	  p = prev;
	}
	prices[tradeKey][i] = p;
      }
      tradereader.close(); 

    }
    else if (calculation == "historical") {
      assert(historicalPrices.size()); 
    
      Logger::logStream(Logger::Debug) << tradeKey << " : "; 
      prices[tradeKey].resize(gameDays - gameStartDays);
      objiter hp1 = historicalPrices.begin();
      int currDays = days((*hp1)->getKey());
      double initPrice = (*hp1)->safeGetFloat(tradeKey, 5);
      double endPrice = 0;
      int nextDays = 0;
      
      Logger::logStream(Logger::Debug) << currDays << " " << initPrice << " "; 
      for (int i = gameStartDays; i < currDays; ++i) {
	prices[tradeKey][i-gameStartDays] = initPrice;
      }
      objiter hp2 = hp1;
      for (++hp2; hp2 != historicalPrices.end(); ++hp2) {
	initPrice = (*hp1)->safeGetFloat(tradeKey, 5);
	endPrice  = (*hp2)->safeGetFloat(tradeKey, 5);
	currDays  = days((*hp1)->getKey());
	nextDays  = days((*hp2)->getKey());
	Logger::logStream(Logger::Debug) << "(" << currDays << " " << nextDays << " " << initPrice << " " << endPrice << ") ";
	
	for (int i = currDays; i < nextDays; ++i) {
	  if (i < gameStartDays) continue;
	  if (i >= gameDays) break; 
	  double price = initPrice + (endPrice - initPrice)*(i-currDays)/(nextDays - currDays);
	  prices[tradeKey][i-gameStartDays] = price; 
	}
	++hp1;
      }

      --hp2;
      currDays = days((*hp2)->getKey());
      initPrice = (*hp2)->safeGetFloat(tradeKey, 5);
      //Logger::logStream(Logger::Debug) << currDays << " " << initPrice << " "; 
      for (int i = currDays; i < gameDays; ++i) {
	prices[tradeKey][i-gameStartDays] = initPrice; 
      }
      
      for (int i = 0; i < (int) prices[tradeKey].size(); i += 10000) {
	Logger::logStream(Logger::Debug) << "(" << i << " "
					 << prices[tradeKey][i]
					 << ") ";
      }

      Logger::logStream(Logger::Debug) << "\n"; 
    }
    else {
      supply[tradeKey].resize(gameDays - gameStartDays);
      demand[tradeKey].resize(gameDays - gameStartDays);
     
      if (calculation == "yes") {
	Object* demandObject = (*tradegood)->safeGetObject("demand");
	Object* supplyObject = (*tradegood)->safeGetObject("supply"); 

	for (objiter eu3prov = eu3Provinces.begin(); eu3prov != eu3Provinces.end(); ++eu3prov) {
	  string currOwnerTag = (*eu3prov)->safeGetString("owner",   "NONE");
	  if (currOwnerTag == "NONE") continue;
	  if (0 == eu3ProvinceToHistoryMap[*eu3prov].size()) continue;
	  
	  int numDays = 0;
	  string provinceGood = (*eu3prov)->safeGetString("trade_goods"); 
	  for (vector<ProvinceHistory*>::iterator p = eu3ProvinceToHistoryMap[*eu3prov].begin(); p != eu3ProvinceToHistoryMap[*eu3prov].end(); ++p) {
	    int prevDays = numDays;
	    numDays += (*p)->numDays;
	    if (0 == findEu3CountryByTag((*p)->ownerTag)) continue; 
	    
	    double basedemand = 0.001*(*p)->basetax;
	    if (demandObject) {
	      objvec mods = demandObject->getValue("modifier");
	      for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
		basedemand *= demandModifier((*mod), (*p), (*eu3prov), numDays);
	      }
	    }
	    
	    double basesupply = 0;
	    if ((provinceGood == tradeKey) && (supplyObject)) {
	      basesupply = 0.05*(*p)->basetax + min(0.99 + (*p)->citysize/101000.0, 2.0);
	      
	      objvec mods = supplyObject->getValue("modifier");
	      for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
		basesupply *= demandModifier((*mod), (*p), (*eu3prov), numDays);
	      }
	    }
	    
	    for (int i = prevDays; i < numDays; ++i) {
	      demand[tradeKey][i] += basedemand; 
	      supply[tradeKey][i] += basesupply*0.01;
	    }
	  }
	}
      }
      else {
	for (unsigned int i = 0; i < supply[tradeKey].size(); ++i) {
	  supply[tradeKey][i] = demand[tradeKey][i] = 1; 
	}
      }
    
      double basePrice = (*tradegood)->safeGetFloat("base_price", 1);
      Logger::logStream(Logger::Debug) << tradeKey << " : "; 
      for (unsigned int i = 0; i < demand[tradeKey].size(); ++i) {
	double actualSupply = max(supply[tradeKey][i], 0.01);	
	double actualDemand = demand[tradeKey][i];
	actualDemand = min(actualDemand, (*tradegood)->safeGetFloat("max_demand", 2.0));
	actualDemand = max(actualDemand, (*tradegood)->safeGetFloat("min_demand", 0.01));
	actualSupply = min(actualSupply, 2.0); 
	
	prices[tradeKey][i] = basePrice*actualDemand * (2.25 - actualSupply);
	if (0 == i % 10000) Logger::logStream(Logger::Debug) << "(" << (int) i << " "
							     << actualDemand << " "
							     << actualSupply << " "
							     << prices[tradeKey][i]
							     << ") ";
      }

      if ((calculation == "yes") && (configObject->safeGetString("writeCache", "no") == "yes")) {
	ofstream tradewriter;
	string filename("./priceCache/");
	filename += tradeKey;
	tradewriter.open(filename.c_str());
	for (unsigned int i = 0; i < prices[tradeKey].size(); ++i) {
	  tradewriter << prices[tradeKey][i] << " "; 
	}
	tradewriter.close(); 
      }
    }
    //Logger::logStream(Logger::Debug) << (int) demand[tradeKey].size() << "\n"; 
  }
  Logger::logStream(Logger::Game) << "Done with prices.\n"; 
}

bool WorkerThread::isOverseasEu3Provinces (Object* oneProv, Object* twoProv) {
  if (!oneProv) return false;
  if (!twoProv) return false;
  if ((oneProv->safeGetString("continent", "NONE") != "NONE") &&
      (twoProv->safeGetString("continent", "NONE") != "NONE") &&
      (oneProv->safeGetString("continent", "NONE") != twoProv->safeGetString("continent", "NONE"))) return true;
  if (0 == eu3ProvinceToVicProvincesMap[oneProv].size()) return false;
  if (0 == eu3ProvinceToVicProvincesMap[twoProv].size()) return false;

  return isOverseasVicProvinces(eu3ProvinceToVicProvincesMap[oneProv][0], eu3ProvinceToVicProvincesMap[twoProv][0]);
}

bool WorkerThread::isOverseasVicProvinces (Object* oneProv, Object* twoProv) {
  if (!oneProv) return false;
  if (!twoProv) return false;
  if (oneProv->safeGetString("continent", "nocon") != twoProv->safeGetString("continent", "somecon")) return true;
  return false;
}

void WorkerThread::calculateTradePower () {
  Object* trade = eu3Game->safeGetObject("trade");
  if (!trade) return; 
  objvec cots = trade->getValue("cot");
  for (objiter cot = cots.begin(); cot != cots.end(); ++cot) {
    int monopoly = 20 - (*cot)->safeGetInt("level");
    double value = (*cot)->safeGetFloat("value");
    for (objiter eu3 = eu3Countries.begin(); eu3 != eu3Countries.end(); ++eu3) {
      Object* entry = (*cot)->safeGetObject((*eu3)->getKey());
      if (!entry) continue;
      int level = entry->safeGetInt("level");
      if (6 == level) level += monopoly;
      (*eu3)->resetLeaf("totalTrade", level * 0.05 * value + (*eu3)->safeGetFloat("totalTrade", 0)); 
    }
  }

  for (objiter prov = eu3Provinces.begin(); prov != eu3Provinces.end(); ++prov) {
    Object* owner = findEu3CountryByTag((*prov)->safeGetString("owner"));
    if (!owner) continue;
    owner->resetLeaf("customs_houses", (*prov)->safeGetFloat("customs_house_fraction")*0.01 + owner->safeGetInt("customs_houses"));
  }

  Object* tradetechobject = 0;
  for (objiter tech = techs.begin(); tech != techs.end(); ++tech) {
    if ((*tech)->getKey() != "trade_tech") continue;
    tradetechobject = (*tech);
    break;
  }
  objvec tradetechs;
  if (tradetechobject) tradetechs = tradetechobject->getValue("technology"); 
  
  double worldTrade = 0;
  for (objiter eu3 = eu3Countries.begin(); eu3 != eu3Countries.end(); ++eu3) {
    int tradeTech = getTechLevel((*eu3), "trade_tech");
    double tradeEfficiency = 0; 
    for (objiter tech = tradetechs.begin(); tech != tradetechs.end(); ++tech) {
      if (tradeTech != (*tech)->safeGetInt("id")) continue;
      tradeEfficiency += (*tech)->safeGetFloat("trade_efficiency");
      break; 
    }

    for (map<string, Object*>::iterator idea = ideasMap.begin(); idea != ideasMap.end(); ++idea) {
      if ((*eu3)->safeGetString((*idea).first, "no") != "yes") continue;
      tradeEfficiency += (*idea).second->safeGetFloat("trade_efficiency"); 
    }

    int pluto = (*eu3)->safeGetInt("aristocracy_plutocracy");
    if (pluto > 0) tradeEfficiency += 0.01*pluto;
    int freet = (*eu3)->safeGetInt("mercantilism_freetrade");
    if (freet > 0) tradeEfficiency += 0.02*freet; 
    
    (*eu3)->resetLeaf("totalTrade", (1 + 0.01 * (*eu3)->safeGetInt("customs_houses")) * (*eu3)->safeGetFloat("totalTrade") * tradeEfficiency);
    worldTrade += (*eu3)->safeGetFloat("totalTrade");
  }
  worldTrade = 1.0 / worldTrade; 
  for (objiter eu3 = eu3Countries.begin(); eu3 != eu3Countries.end(); ++eu3) {
    (*eu3)->resetLeaf("totalTrade", worldTrade * (*eu3)->safeGetFloat("totalTrade"));
  }  
}

int WorkerThread::getTechLevel (Object* eu3country, string techtype) {
  Object* eu3Tech = eu3country->safeGetObject("technology");
  if (!eu3Tech) return 0; 
  Object* eu3Group = eu3Tech->safeGetObject(techtype);
  if (!eu3Group) return 0;
  return atoi(eu3Group->getToken(0).c_str());
}

void WorkerThread::calculateCustomPoints () {
  for (objiter prov = eu3Provinces.begin(); prov != eu3Provinces.end(); ++prov) {
    Object* owner = findEu3CountryByTag((*prov)->safeGetString("owner"));
    if (!owner) continue;
    for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
      int customPoints = (*b)->safeGetInt("customPoints");
      if (0 == customPoints) continue;
      if (!hasBuildingOrBetter((*b)->getKey(), (*prov))) continue;
      owner->resetLeaf("customPoints", owner->safeGetInt("customPoints") + customPoints);
    }
  }

  for (objiter eu3 = eu3Countries.begin(); eu3 != eu3Countries.end(); ++eu3) {
    double cpoints = (*eu3)->safeGetFloat("customPoints");
    Logger::logStream(Logger::Game) << (*eu3)->getKey() << " custom points: " << cpoints << "\n"; 
    
    Object* currCustom = customObject->safeGetObject((*eu3)->getKey());
    if (!currCustom) continue;
    objvec bids = currCustom->getLeaves();
    double total = 0;
    for (objiter bid = bids.begin(); bid != bids.end(); ++bid) {
      if (!(*bid)->isLeaf()) continue;
      double curr = atof((*bid)->getLeaf().c_str());
      if (0 > curr) Logger::logStream(Logger::Game) << "  Negative bid for " << (*bid)->getKey() << " by " << (*eu3)->getKey() << " disregarded.\n";
      else total += curr;
    }
    if (fabs(total - 1) < 0.0001) continue;
    if (total < 0.0001) continue;
    Logger::logStream(Logger::Game) << "  Total bids " << total << " not allowed, adjusting custom points to " << (cpoints / total) << "\n";
    (*eu3)->resetLeaf("customPoints", (cpoints / total));
  }
}

void WorkerThread::techLevels () {
  // Civilised status from having four groups at level 72,
  // or at date level. 
  // Tech points from EU3 techs, maximum of 150k, end-weighted.
  // Bonus from custom points and trade level. 
  // Establishment from most-advanced group. 

  for (map<Object*, objvec>::iterator i = vicCountryToEu3CountriesMap.begin(); i != vicCountryToEu3CountriesMap.end(); ++i) {
    Object* vicCountry = (*i).first;
    vicCountry->resetLeaf("civilized", "no");
    Object* vicTech = vicCountry->safeGetObject("technology");
    if (!vicTech) {
      vicTech = new Object("technology");
      vicCountry->setValue(vicTech); 
    }
    vicTech->clear(); 
    Object* eu3Country = (*i).second[0];
    Object* eu3Tech = eu3Country->safeGetObject("technology");
    if (!eu3Tech) continue;  // Uncivilised and unteched...
    if (vicCountry->getKey() == "REB") continue; 
    
    double techPoints = 0; 
    int upToDate = 0;
    Object* highestTech = 0;
    Object* secondTech = 0;
    pair<int, int> highestLevels(0, 0); 
    for (objiter group = techs.begin(); group != techs.end(); ++group) {
      int eu3Level = getTechLevel(eu3Country, (*group)->getKey());
      Object* eu3Group = eu3Tech->safeGetObject((*group)->getKey()); 
      if (eu3Level > highestLevels.first) {
	highestLevels.second = highestLevels.first;
	secondTech = highestTech;
	highestTech = eu3Group;
	highestLevels.first = eu3Level;
      }
      else if (eu3Level > highestLevels.second) {
	secondTech = eu3Group;
	highestLevels.second = eu3Level; 
      }
      techPoints += configObject->safeGetFloat("rp_per_area", 30000)*pow(eu3Level / configObject->safeGetFloat("rp_denominator", 80.0),
									 configObject->safeGetFloat("rp_exponent", 2)); 
      
      if (eu3Level >= configObject->safeGetInt("civLevel")) {
	upToDate++;
	continue; 
      }
      
      eu3Level++; // Count as one higher to avoid having to be ahead-of-time. Up to date is sufficient. 
      objvec techs = (*group)->getValue("technology");
      for (objiter tech = techs.begin(); tech != techs.end(); ++tech) {
	int currLevel = (*tech)->safeGetInt("id");
	if (currLevel != eu3Level) continue;
	int currDays = (*tech)->safeGetInt("days");
	if (currDays >= gameDays) upToDate++;
	break; 
      }
    }
    if (upToDate >= 4) vicCountry->resetLeaf("civilized", "yes");
    Object* currCustom = customObject->safeGetObject(eu3Country->getKey());
    if (!currCustom) currCustom = customObject->getNeededObject("DUMMY"); 
    Logger::logStream(Logger::Game) << eu3Country->getKey()
				    << " RPs (base, trade bonus, custom) : "
				    << techPoints << " "
				    << techPoints*eu3Country->safeGetFloat("totalTrade") * configObject->safeGetFloat("tradeResearchBonus")
				    << eu3Country->safeGetFloat("customPoints") * currCustom->safeGetFloat("rp") * configObject->safeGetFloat("rpPerCustom", 10)
				    << ".\n"; 
    
    techPoints *= (1 + eu3Country->safeGetFloat("totalTrade") * configObject->safeGetFloat("tradeResearchBonus"));
    techPoints += eu3Country->safeGetFloat("customPoints") * currCustom->safeGetFloat("rp") * configObject->safeGetFloat("rpPerCustom", 10); 
    vicCountry->resetLeaf("research_points", techPoints);
    string school = "\"traditional_academic\""; 
    if ((highestTech) && (highestLevels.first > highestLevels.second)) {
      if (highestTech->getKey() == "land_tech") school = "\"army_tech_school\"";
      else if (highestTech->getKey() == "naval_tech") school = "\"naval_tech_school\"";
      else if (highestTech->getKey() == "trade_tech") school = "\"commerce_tech_school\"";
      else if (highestTech->getKey() == "production_tech") school = "\"industrial_tech_school\"";
      else school = "\"culture_tech_school\""; 
    }
    vicCountry->resetLeaf("schools", school); 

    if (targetVersion == ".\\AHD\\") {
      Object* countryCustom = customObject->safeGetObject(eu3Country->getKey());
      if ((!countryCustom) || (0 == countryCustom->safeGetObject("research"))) countryCustom = customObject->safeGetObject("DUMMY");
      assert(countryCustom);
      Object* research = countryCustom->safeGetObject("research");
      objvec techlist = research->getValue("tech");
      map<string, bool> alreadyGot;
      map<Object*, int> attempts; 
      objvec allTechs = vicTechs->getLeaves();
      Object* schools = vicTechs->safeGetObject("schools");
      Object* techSchool = 0;
      if (schools) techSchool = schools->safeGetObject(remQuotes(school));
      objvec retries;
      int numTries = 0; 
    retryBadTechs: 
      numTries++; 
      for (objiter tech = techlist.begin(); tech != techlist.end(); ++tech) {
	string techname = (*tech)->getLeaf();
	if (alreadyGot[techname]) continue;	
	Object* vtech = vicTechs->safeGetObject(techname);
	if (!vtech) {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find Vicky tech \""
					     << techname
					     << "\"\n"; 
	  continue; 
	}
	if (1837 < vtech->safeGetInt("year")) continue; 
		
	double cost = vtech->safeGetFloat("cost");
	double bonus = 0; 
	if (techSchool) {
	  string techArea = vtech->safeGetString("area");
	  string techBonus = schools->safeGetString(techArea);
	  bonus = techSchool->safeGetFloat(techBonus, 0);
	}
	cost *= (1 - bonus); 
       
	if (cost > techPoints) {
	  Logger::logStream(Logger::Warning) << "Tech "
					     << vtech->getKey()
					     << " too expensive for "
					     << vicCountry->getKey()
					     << "\n"; 
	  continue; 
	}
	else {
	  Logger::logStream(Logger::Debug) << "Tech "
					   << vtech->getKey()
					   << " costs "
					   << cost
					   << " (base "
					   << (cost / (1 - bonus))
					   << " with bonus "
					   << bonus
					   << " from "
					   << school << " "
					   << (int) techSchool
					   << ") for country "
					   << vicCountry->getKey()
					   << ".\n"; 
	}

	Object* prereq = 0; 
	for (objiter t = allTechs.begin(); t != allTechs.end(); ++t) {
	  if ((*t)->getKey() == techname) break;
	  if ((*t)->safeGetString("area") != vtech->safeGetString("area")) continue;
	  if (alreadyGot[(*t)->getKey()]) continue;
	  prereq = (*t);
	  break; 
	}

	if (prereq) {
	  Logger::logStream(Logger::Warning) << "Warning: Attempt to get tech "
					     << techname
					     << " without prerequisite "
					     << prereq->getKey()
					     << ", de-prioritising.\n";
	  if (attempts[*tech]++ < 3) retries.push_back(*tech);
	  continue; 
	}
	
	techPoints -= cost;
	alreadyGot[techname] = true;
	Object* vlist = new Object(techname);
	vlist->setObjList(true);
	vlist->addToList("1");
	vlist->addToList("0.000");
	vicTech->setValue(vlist); 
      }

      if (0 < retries.size()) {
	techlist.clear();
	for (objiter t = retries.begin(); t != retries.end(); ++t) techlist.push_back(*t);
	retries.clear(); 
	goto retryBadTechs;
      }

      if ((techPoints > 3600) && (numTries < 3)) {
	countryCustom = customObject->safeGetObject("DUMMY");
	Object* research = countryCustom->safeGetObject("research");
	techlist = research->getValue("tech");	
	goto retryBadTechs; 
      }
      
      vicCountry->resetLeaf("research_points", techPoints);      
    }
    
  }
}

void WorkerThread::simplify () {
  if (!eu3Game) return;
  objvec leaves = eu3Game->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    //for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
      //(*leaf)->unsetValue((*b)->getKey()); 
    //}

    if (0 < (*leaf)->safeGetFloat("manpower", -1)) (*leaf)->resetLeaf("manpower", "1.000");
    //if (0 < (*leaf)->safeGetFloat("citysize", -1)) (*leaf)->resetLeaf("citysize", "1000");
    //if (0 < (*leaf)->safeGetFloat("base_tax", -1)) (*leaf)->resetLeaf("base_tax", "1.000");
  }

  ofstream writer;  
  writer.open(".\\Output\\simplified.eu3");
  Parser::topLevel = eu3Game;
  writer << (*eu3Game);
  writer.close();
  Logger::logStream(Logger::Game) << "Wrote simplified file.\n"; 
}

void WorkerThread::tradeStats () {
  QDir tradeDir("TradeStats");
  QStringList fileList = tradeDir.entryList(QStringList(QString("*.eu3")));
  //std::vector<int> gotYears;

  // Need this for modifiers - otherwise done in initialise. 
  sliders.push_back("aristocracy_plutocracy");
  sliders.push_back("centralization_decentralization");
  sliders.push_back("innovative_narrowminded");
  sliders.push_back("mercantilism_freetrade");
  sliders.push_back("offensive_defensive");
  sliders.push_back("land_naval");
  sliders.push_back("quality_quantity");
  sliders.push_back("serfdom_freesubjects");


  Object* prices = new Object("prices"); 
  for (int i = 0; i < fileList.size(); ++i) {
    std::string currFileName("TradeStats/");
    currFileName += fileList.at(i).toAscii().data();
    //Logger::logStream(Logger::Game) << currFileName << "\n";
    
    eu3Game = loadTextFile(currFileName);
    int year = 0;
    string dateString = remQuotes(eu3Game->safeGetString("date", "\"1.1.1\""));
    if (dateString == "1.1.1") continue; 
    days(dateString, &year);
    //if (find(gotYears.begin(), gotYears.end(), year) != gotYears.end()) continue;
    //gotYears.push_back(year);
    sprintf(strbuffer, "%i", year);
    if (prices->safeGetObject(strbuffer)) continue;    
    Object* currentYear = new Object(strbuffer);
    prices->setValue(currentYear); 

    map<string, bool> warMap;
    objvec wars = eu3Game->getValue("active_war");
    for (objiter war = wars.begin(); war != wars.end(); ++war) {
      objvec attackers = (*war)->getValue("attacker");
      for (objiter attack = attackers.begin(); attack != attackers.end(); ++attack) {
	warMap[remQuotes((*attack)->getLeaf())] = true;
      }
      objvec defenders = (*war)->getValue("defender");
      for (objiter defend = defenders.begin(); defend != defenders.end(); ++defend) {
	warMap[remQuotes((*defend)->getLeaf())] = true;
      }      
    }

    double totalBaseTax = 0;
    double totalManpower = 0;
    double totalCitySize = 0;
    eu3Provinces.clear(); 
    for (int i = 0; i < 2000; ++i) {
      sprintf(strbuffer, "%i", i);
      Object* prov = eu3Game->safeGetObject(strbuffer);
      if (!prov) continue;
      eu3Provinces.push_back(prov);
      if (prov->safeGetString("owner", "NONE") != "NONE") {
	totalBaseTax  += prov->safeGetFloat("base_tax");
	totalManpower += prov->safeGetFloat("manpower");
	totalCitySize += prov->safeGetFloat("citysize"); 
      }
    }

    objvec continents = eu3ContinentObject->getLeaves();
    for (objiter cont = continents.begin(); cont != continents.end(); ++cont) {
      for (int i = 0; i < (*cont)->numTokens(); ++i) {
	string tag = (*cont)->getToken(i);
	Object* prov = eu3Game->safeGetObject(tag);
	if (!prov) continue;
	prov->resetLeaf("continent", (*cont)->getKey()); 
      }
    }

    
    //Logger::logStream(Logger::Game) << "Total tax, mp, city: " << totalBaseTax << ", " << totalManpower << ", " << totalCitySize << "\n"; 
    
    objvec leaves = eu3Game->getLeaves();
    for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
      objvec armies = (*leaf)->getValue("army");
      int infantry  = 0;
      int artillery = 0;
      int cavalry   = 0;      
      for (objiter army = armies.begin(); army != armies.end(); ++army) {
	string loc = (*army)->safeGetString("location");
	Object* prov = eu3Game->safeGetObject(loc);
	if (!prov) continue;
	objvec regiments = (*army)->getValue("regiment");
	for (objiter reg = regiments.begin(); reg != regiments.end(); ++reg) {
	  string regtype = remQuotes((*reg)->safeGetString("type"));
	  regtype = unitType(regtype);
	  if (regtype == "infantry") infantry++;
	  else if (regtype == "cavalry") cavalry++;
	  else artillery++; 	  
	}
	prov->resetLeaf("regiments", prov->safeGetInt("regiments") + (int) regiments.size()); 
      }
      if (0 < armies.size()) {
	(*leaf)->resetLeaf("infRegiments", (*leaf)->safeGetInt("infRegiments") + infantry);
	(*leaf)->resetLeaf("cavRegiments", (*leaf)->safeGetInt("cavRegiments") + cavalry);
	(*leaf)->resetLeaf("artRegiments", (*leaf)->safeGetInt("artRegiments") + artillery);
      }

      Object* ledger = (*leaf)->safeGetObject("ledger");
      if (ledger) {
	ledger = ledger->safeGetObject("lastmonthincometable");
	if (ledger) {
	  double totalIncome = 0;
	  double tradeIncome = ledger->tokenAsFloat(1);
	  double goldIncome  = ledger->tokenAsFloat(3);
	  for (int i = 0; i < ledger->numTokens(); ++i) {
	    totalIncome += ledger->tokenAsFloat(i);
	  }
	  if (0 < totalIncome) {
	    (*leaf)->resetLeaf("tradePercentage", tradeIncome / totalIncome);
	    (*leaf)->resetLeaf("goldPercentage",  goldIncome  / totalIncome);
	  }
	}
      }

      objvec dips = (*leaf)->getLeaves();
      for (objiter dip = dips.begin(); dip != dips.end(); ++dip) {
	if ((*dip)->safeGetString("trade_refusal", "no") == "yes") (*leaf)->resetLeaf("numEmbargos", 1 + (*leaf)->safeGetInt("numEmbargos"));
      }
    }
    
    map<string, double> demand;
    map<string, double> supply;    
    eu3ProvinceToHistoryMap.clear();
    for (objiter eu3prov = eu3Provinces.begin(); eu3prov != eu3Provinces.end(); ++eu3prov) {
      ProvinceHistory* curr = new ProvinceHistory();
      
      curr->ownerTag = remQuotes((*eu3prov)->safeGetString("owner"));
      Object* ownerNation = eu3Game->safeGetObject(curr->ownerTag);
      if (!ownerNation) continue;
      eu3TagToEu3CountryMap[curr->ownerTag] = ownerNation; 
      eu3ProvinceToHistoryMap[*eu3prov].push_back(curr);

      curr->religion = (*eu3prov)->safeGetString("religion");
      curr->controllerTag = remQuotes((*eu3prov)->safeGetString("controller"));
      curr->citysize = (*eu3prov)->safeGetFloat("citysize");
      curr->manpower = (*eu3prov)->safeGetFloat("manpower");
      curr->basetax = (*eu3prov)->safeGetFloat("base_tax");
    
      for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
	if (!hasBuildingOrBetter((*b)->getKey(), (*eu3prov))) continue;
	curr->buildings[(*b)->getKey()] = true; 
      }

      curr->ownerReligion   = ownerNation->safeGetString("religion");
      curr->ownerGovernment = ownerNation->safeGetString("government"); 
      curr->ownerAtWar = warMap[curr->ownerTag];

      if (heuristicIsCoastal(*eu3prov)) ownerNation->resetLeaf("num_ports", 1 + ownerNation->safeGetInt("num_ports"));
      curr->capital = eu3Game->safeGetObject(ownerNation->safeGetString("capital", "dhsajkl")); 
    }
    
    for (objiter tradegood = goods.begin(); tradegood != goods.end(); ++tradegood) {
      string tradeKey = (*tradegood)->getKey(); 
      Object* demandObject = (*tradegood)->safeGetObject("demand");
      Object* supplyObject = (*tradegood)->safeGetObject("supply"); 

      double totalUnits = 0;
      double totalTruncUnits = 0; 
      for (objiter eu3prov = eu3Provinces.begin(); eu3prov != eu3Provinces.end(); ++eu3prov) {
	string currOwnerTag = (*eu3prov)->safeGetString("owner",   "NONE");
	if (currOwnerTag == "NONE") continue;
	  
	string provinceGood = (*eu3prov)->safeGetString("trade_goods");
	double units = 0.05*(*eu3prov)->safeGetFloat("base_tax") + min(0.99 + (*eu3prov)->safeGetFloat("citysize")/101000.0, 2.0);
	if (units < 1) units = 1; 
	totalUnits += units;
	// Truncate, don't round
	units = 0.1*floor(units*10);
	totalTruncUnits += units; 

	// Empirically arrived at. Apparently 4000 basetax is exactly 100% demand, all modifiers absent. 
	double basedemand = 0.00025*(*eu3prov)->safeGetFloat("base_tax");
	if (demandObject) {
	  objvec mods = demandObject->getValue("modifier");
	  for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
	    basedemand *= demandModifier((*mod), 0, (*eu3prov), -1);
	  }
	}
	    
	double basesupply = 0;
	if ((provinceGood == tradeKey) && (supplyObject)) {
	  basesupply = units;
	  
	  objvec mods = supplyObject->getValue("modifier");
	  for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
	    basesupply *= demandModifier((*mod), 0, (*eu3prov), -1);
	  }
	}

	
	demand[tradeKey] += basedemand; 
	supply[tradeKey] += basesupply*0.01;
      }
    }

    for (objiter tradegood = goods.begin(); tradegood != goods.end(); ++tradegood) {
      string tradeKey = (*tradegood)->getKey();     
      double basePrice = (*tradegood)->safeGetFloat("base_price", 1);
      double actualSupply = max(supply[tradeKey], 0.01);	
      double actualDemand = demand[tradeKey];
      actualDemand = min(actualDemand, (*tradegood)->safeGetFloat("max_demand", 2.0));
      actualDemand = max(actualDemand, (*tradegood)->safeGetFloat("min_demand", 0.01));
      actualSupply = min(actualSupply, 2.0); 
	
      basePrice *= actualDemand * (2.25 - actualSupply);
      Logger::logStream(Logger::Game) << tradeKey << " = "
				      << basePrice << "\n";
      currentYear->setLeaf(tradeKey, basePrice); 
    }    

    delete eu3Game;
  }

  ofstream writer;  
  writer.open(".\\Output\\historicalPrices.txt");
  Parser::topLevel = prices;
  writer << (*prices);
  writer.close();
  Logger::logStream(Logger::Game) << "Wrote price file.\n"; 
  
}

Object* WorkerThread::addUnitToHigher (string vickey, string victype, int& numRegiments, Object* army, Object* vicCountry, string eu3Name, double vicStr) {
  if (0 == army->getValue(vickey).size()) {
    army->setValue(makeIdObject(vicGame->safeGetInt("unit"), 40));
    vicCountry->setValue(army);
    vicGame->resetLeaf("unit", 1 + vicGame->safeGetInt("unit")); 
  }
  Object* vicRegiment = new Object(vickey); 
  vicRegiment->setValue(makeIdObject(numRegiments++, 40));
  vicRegiment->setLeaf("type", victype);
  vicRegiment->setLeaf("strength", vicStr);
  vicRegiment->setLeaf("organisation", "10.000");
  vicRegiment->setLeaf("name", eu3Name); 
  army->setValue(vicRegiment);
  return vicRegiment; 
}

void WorkerThread::createRegiment (int& number, string victype, int& numRegiments, Object* army, Object* vicCountry, string eu3Name) {
  if (number < regimentRatio[victype]) return;
  number = 0;

  Object* vicRegiment = addUnitToHigher("regiment", victype, numRegiments, army, vicCountry, eu3Name, 3.0); 
  if (vicRegiment) vicCountry->resetLeaf("numUnits", 1 + vicCountry->safeGetInt("numUnits"));
  /*
  Logger::logStream(Logger::Debug) << "Reset "
				   << vicCountry->getKey()
				   << " number of units to "
				   << vicCountry->safeGetInt("numUnits")
				   << " due to "
				   << vicRegiment->safeGetString("name") 
				   << ".\n"; 
  */ 
  // Find a supporting soldier POP.
  Object* bestSoldier = 0;
  double bestRatio = 0; 
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    if (vicCountry->getKey() != remQuotes((*vp)->safeGetString("owner"))) continue; 
    objvec soldiers = (*vp)->getValue("soldiers");
    for (objiter soldier = soldiers.begin(); soldier != soldiers.end(); ++soldier) {
      double ratio = (*soldier)->safeGetFloat("size", 1.0);
      ratio /= max(1, (*soldier)->safeGetInt("supported"));
      if ((0 == bestSoldier) || (ratio > bestRatio)) {
	bestSoldier = (*soldier);
	bestRatio = ratio; 
      }
    }
  }

  if (!bestSoldier) {
    Object* capital = vicGame->safeGetObject(vicCountry->safeGetString("capital"));
    assert(capital); 
    
    int popid = vicGame->safeGetInt("start_pop_index");
    bestSoldier = new Object("soldiers");
    bestSoldier->setLeaf("id", popid++);
    bestSoldier->setLeaf("size", 500); 
    bestSoldier->setLeaf("literacy", 0.10);
    string dummy(""); 
    resetCulture(bestSoldier, vicProvinceToEu3ProvincesMap[capital][0], dummy); 
    vicGame->resetLeaf("start_pop_index", popid);
    capital->setValue(bestSoldier); 
  }

  Object* supPop = new Object("pop");
  supPop->setLeaf("id", bestSoldier->safeGetString("id"));
  supPop->setLeaf("type", "46");
  vicRegiment->setValue(supPop);
  bestSoldier->resetLeaf("supported", 1 + bestSoldier->safeGetInt("supported")); 
}

void WorkerThread::convertArmies () {
  Object* unitConversions = configObject->safeGetObject("units");
  if (!unitConversions) unitConversions = new Object("units"); 

  int infantry = 0;
  int cavalry = 0;
  int artillery = 0; 
  int irregular = 0;
  int menowars = 0;
  int frigates = 0;
  int clippers = 0; 
  for (map<Object*, objvec>::iterator i = vicCountryToEu3CountriesMap.begin(); i != vicCountryToEu3CountriesMap.end(); ++i) {
    Object* eu3Country = (*i).second[0];
    objvec armies = eu3Country->getValue("army");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regiments = (*army)->getValue("regiment");
      for (objiter reg = regiments.begin(); reg != regiments.end(); ++reg) {
	string eu3type = remQuotes((*reg)->safeGetString("type"));
	string victype = unitConversions->safeGetString(eu3type, "irregular");
	if      (victype == "infantry")  infantry++;
	else if (victype == "cavalry")   cavalry++;
	else if (victype == "artillery") artillery++;
	else                             irregular++;	
      }
    }

    objvec navies = eu3Country->getValue("navy");

    for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
      objvec eu3Ships = (*navy)->getValue("ship");
      for (objiter eu3Ship = eu3Ships.begin(); eu3Ship != eu3Ships.end(); ++eu3Ship) {
  	string eu3type = remQuotes((*eu3Ship)->safeGetString("type"));
	string victype = unitConversions->safeGetString(eu3type, "irregular");
	if      (victype == "manowar") menowars++;
	else if (victype == "frigate") frigates++;
	else if (victype == "clipper_transport") clippers++;
      }
    }
  }

  regimentRatio["infantry"]          = max(infantry  / regimentRatio["infantry"],         1.0);  
  regimentRatio["cavalry"]           = max(cavalry   / regimentRatio["cavalry"],          1.0);   
  regimentRatio["artillery"]         = max(artillery / regimentRatio["artillery"],        1.0); 
  regimentRatio["irregular"]         = max(irregular / regimentRatio["irregular"],        1.0);
  regimentRatio["frigate"]           = max(frigates  / regimentRatio["frigate"],          1.0);
  regimentRatio["clipper_transport"] = max(clippers / regimentRatio["clipper_transport"], 1.0);
  regimentRatio["manowar"]           = max((0.1*frigates + menowars)  / regimentRatio["manowar"],          1.0);

  Logger::logStream(Logger::Game) << "Unit ratios: "
				  << "\n  Infantry   : " << regimentRatio["infantry"] 
				  << "\n  Cavalry    : " << regimentRatio["cavalry"] 
    				  << "\n  Artillery  : " << regimentRatio["artillery"] 
				  << "\n  Irregulars : " << regimentRatio["irregular"] 
				  << "\n  Men'o'war  : " << regimentRatio["manowar"] 
				  << "\n  Frigates   : " << regimentRatio["frigate"] 
				  << "\n  Clippers   : " << regimentRatio["clipper_transport"] 
				  << "\n"; 

  
  int numRegiments = 1; 
  for (map<Object*, objvec>::iterator i = vicCountryToEu3CountriesMap.begin(); i != vicCountryToEu3CountriesMap.end(); ++i) {
    Object* vicCountry = (*i).first;
    Object* eu3Country = (*i).second[0];
    if (eu3Country->getKey() == "REB") continue;
    if (eu3Country->getKey() == "PIR") continue; 
    objvec armies = eu3Country->getValue("army");

    map<string, Object*> armyMap;
    string eu3Location; 
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regiments = (*army)->getValue("regiment");
      for (objiter reg = regiments.begin(); reg != regiments.end(); ++reg) {
	string eu3type = remQuotes((*reg)->safeGetString("type"));
	string victype = unitConversions->safeGetString(eu3type, "irregular");
	if      (victype == "infantry")  infantry++;
	else if (victype == "cavalry")   cavalry++;
	else if (victype == "artillery") artillery++;
	else                             irregular++;
	
	string eu3Location = (*army)->safeGetString("location");
	if (!armyMap[eu3Location]) {
	  armyMap[eu3Location] = new Object("army");
	  armyMap[eu3Location]->setLeaf("name", (*army)->safeGetString("name", "\"Nameless Army\""));
	  Object* eu3Province = eu3Game->safeGetObject(eu3Location);
	  Object* vicProvince = 0; 
	  if ((!eu3Province) || (0 == eu3ProvinceToVicProvincesMap[eu3Province].size())) vicProvince = vicGame->safeGetObject(vicCountry->safeGetString("capital"));
	  else vicProvince = eu3ProvinceToVicProvincesMap[eu3Province][0];
	  assert(vicProvince); 
	  armyMap[eu3Location]->setLeaf("location", vicProvince->getKey()); 
	}
	
	createRegiment(infantry,  "infantry",  numRegiments, armyMap[eu3Location], vicCountry, (*reg)->safeGetString("name", "\"Unnamed Regiment\""));
	createRegiment(cavalry,   "cavalry",   numRegiments, armyMap[eu3Location], vicCountry, (*reg)->safeGetString("name", "\"Unnamed Regiment\""));
	createRegiment(artillery, "artillery", numRegiments, armyMap[eu3Location], vicCountry, (*reg)->safeGetString("name", "\"Unnamed Regiment\""));
	createRegiment(irregular, "irregular", numRegiments, armyMap[eu3Location], vicCountry, (*reg)->safeGetString("name", "\"Unnamed Regiment\""));
      }
    }
    if (armyMap[eu3Location]) {
      if (infantry > 0) {
	infantry = 1 + regimentRatio["infantry"];
	createRegiment(infantry,  "infantry",  numRegiments, armyMap[eu3Location], vicCountry, "\"Extra Infantry\"");
      }
      if (cavalry > 0) {
	cavalry = 1 + regimentRatio["cavalry"];
	createRegiment(cavalry,   "cavalry",   numRegiments, armyMap[eu3Location], vicCountry, "\"Extra Cavalry\"");
      }
      if (artillery > 0) {
	artillery = 1 + regimentRatio["artillery"];
	createRegiment(artillery, "artillery", numRegiments, armyMap[eu3Location], vicCountry, "\"Extra Artillery\"");
      }
      if (irregular > 0) {
	irregular = 1 + regimentRatio["irregular"];
	createRegiment(irregular, "irregular", numRegiments, armyMap[eu3Location], vicCountry, "\"Extra Irregulars\"");
      }
    }

    // Now navies.
    
    objvec navies = eu3Country->getValue("navy");
    if (0 == navies.size()) continue; 

    vector<string> menowar;
    vector<string> frigate;
    vector<string> clipper;
    int frigateAsManowarCount = 0; 
    for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
      objvec eu3Ships = (*navy)->getValue("ship");
      for (objiter eu3Ship = eu3Ships.begin(); eu3Ship != eu3Ships.end(); ++eu3Ship) {
	string eu3type = remQuotes((*eu3Ship)->safeGetString("type"));
	string victype = unitConversions->safeGetString(eu3type, "irregular");
	if      (victype == "manowar") menowar.push_back((*eu3Ship)->safeGetString("name", "\"Generic Big Ship\""));
	else if (victype == "frigate") {
	  frigate.push_back((*eu3Ship)->safeGetString("name", "\"Generic Light Ship\""));
	  if (0 == frigateAsManowarCount++) {
	    menowar.push_back((*eu3Ship)->safeGetString("name", "\"Generic Light Ship\""));
	    if (10 <= frigateAsManowarCount) frigateAsManowarCount = 0; 
	  }
	}
	else if (victype == "clipper_transport") clipper.push_back((*eu3Ship)->safeGetString("name", "\"Generic Transport\""));
      }
    }

    if (0 == menowar.size() + frigate.size() + clipper.size()) continue;
    
    // Find where to put them.
    Object* fleetLocation = 0; 
    for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
      if (vicCountry->getKey() != remQuotes((*vp)->safeGetString("owner"))) continue;
      if (vicCountry->getKey() != remQuotes((*vp)->safeGetString("controller"))) continue;
      if (!heuristicVicIsCoastal(*vp)) continue;
      fleetLocation = (*vp);
      break; 
    }
    if (!fleetLocation) {
      Logger::logStream(Logger::Error) << "Error: Could not find coastal province for fleet of EU3 tag "
				       << eu3Country->getKey()
				       << ", no fleet conversion will be done.\n";
      continue; 
    }

    Object* vicNavy = new Object("navy");
    vicNavy->setLeaf("location", fleetLocation->getKey());
    Logger::logStream(Logger::Debug) << "Navy location: " << nameAndNumber(fleetLocation) << " for " << vicCountry->getKey() << "\n"; 
    //vicNavy->setLeaf("location", "2723"); 
    for (double i = 0; i < menowar.size(); i += regimentRatio["manowar"]) {
      unsigned int mowIdx = (unsigned int) floor(i + 0.5);
      Object* ship = addUnitToHigher("ship", "manowar", numRegiments, vicNavy, vicCountry, mowIdx < menowar.size() ? menowar[mowIdx] : "\"Big Ship\"", 100.0);
      if (ship) vicCountry->resetLeaf("numUnits", 1 + vicCountry->safeGetInt("numUnits")); 
    }
    for (double i = 0; i < frigate.size(); i += regimentRatio["frigate"]) {
      Object* ship = addUnitToHigher("ship", "frigate", numRegiments, vicNavy, vicCountry, frigate[i], 100.0);
      if (ship) vicCountry->resetLeaf("numUnits", 1 + vicCountry->safeGetInt("numUnits")); 
    }
    for (double i = 0; i < clipper.size(); i += regimentRatio["clipper_transport"]) {
      Object* ship = addUnitToHigher("ship", "clipper_transport", numRegiments, vicNavy, vicCountry, clipper[i], 100.0);
      if (ship) vicCountry->resetLeaf("numUnits", 1 + vicCountry->safeGetInt("numUnits")); 
    }
    
  }

  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    objvec soldiers = (*vp)->getValue("soldiers");
    for (objiter soldier = soldiers.begin(); soldier != soldiers.end(); ++soldier) {
      (*soldier)->unsetValue("supported");
    }
  }
}

void WorkerThread::navalBases () {
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    (*vp)->unsetValue("fort");

    int fortlevel = 0; 
    for (objiter eu3prov = vicProvinceToEu3ProvincesMap[*vp].begin(); eu3prov != vicProvinceToEu3ProvincesMap[*vp].end(); ++eu3prov) {
      if ((*eu3prov)->safeGetString("fort6", "no") == "yes") {fortlevel = 2; break;} 
      else if ((*eu3prov)->safeGetString("fort5", "no") == "yes") fortlevel = 1;       
    }

    if (0 != fortlevel) {
      Object* vfort = new Object("fort");
      vfort->setObjList(true);	
      vfort->addToList(2 == fortlevel ? "2.000" : "1.000");
      vfort->addToList(2 == fortlevel ? "2.000" : "1.000");
      (*vp)->setValue(vfort);
    }
    
    if (!heuristicVicIsCoastal(*vp)) continue;
    (*vp)->unsetValue("naval_base");    
    Object* eu3prov = vicProvinceToEu3ProvincesMap[*vp][0];
    if (!eu3prov) continue;
    
    if (eu3prov->safeGetString("doneNavalBase", "no") != "yes") {
      eu3prov->resetLeaf("doneNavalBase", "yes");
      if (eu3prov->safeGetString("naval_base", "no") == "yes") {
	Object* vfort = new Object("naval_base");
	vfort->setObjList(true);
	vfort->addToList("2.000");
	vfort->addToList("2.000");
	(*vp)->setValue(vfort);
      }
      else if (eu3prov->safeGetString("naval_arsenal", "no") == "yes") {
	Object* vfort = new Object("naval_base");
	vfort->setObjList(true);
	vfort->addToList("1.000");
	vfort->addToList("1.000");
	(*vp)->setValue(vfort);
      }
    }
  }
}

std::string WorkerThread::getPopCulture (Object* pop) {
  string cand1Culture = "nekulturny";
  for (map<string, string>::iterator cult = eu3CultureToVicCultureMap.begin(); cult != eu3CultureToVicCultureMap.end(); ++cult) {
    if (pop->safeGetString((*cult).second, "NOTHING") == "NOTHING") continue;
    cand1Culture = (*cult).second;
    break; 
  }
  return cand1Culture; 
}

void WorkerThread::mergePops () {
  // Per-province merging
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    if ((*vp)->safeGetString("canHaveFarmers", "no") == "yes") {
      objvec labour = (*vp)->getValue("labourers");
      for (objiter l = labour.begin(); l != labour.end(); ++l) {
	(*l)->setKey("farmers");
      }
    }
    else {
      objvec farms = (*vp)->getValue("farmers");
      for (objiter f = farms.begin(); f != farms.end(); ++f) {
	(*f)->setKey("labourers"); 
      }
    }
    
    for (map<string, Object*>::iterator name = poptypes.begin(); name != poptypes.end(); ++name) {
      objvec currPops = (*vp)->getValue((*name).first);
      objvec removalList; 
      for (objiter cand1 = currPops.begin(); cand1 != currPops.end(); ++cand1) {
	if (0 == (*cand1)->safeGetInt("size")) {
	  removalList.push_back(*cand1);
	  continue; 
	}
	string cand1Culture = getPopCulture(*cand1);
	string cand1Religion = (*cand1)->safeGetString(cand1Culture);

	for (objiter cand2 = cand1 + 1; cand2 != currPops.end(); ++cand2) {
	  if ((*cand2)->safeGetString(cand1Culture, "NVM") != cand1Religion) continue;
	  removalList.push_back(*cand2);
	  double totalSize = (*cand1)->safeGetInt("size") + (*cand2)->safeGetInt("size");
	  (*cand1)->resetLeaf("literacy", max((*cand1)->safeGetFloat("literacy"), (*cand2)->safeGetFloat("literacy")));
	  (*cand1)->resetLeaf("size", totalSize);
	  if ((!(*cand1)->safeGetObject("issues")) && ((*cand2)->safeGetObject("issues"))) (*cand1)->setValue((*cand2)->safeGetObject("issues"));
	  if ((!(*cand1)->safeGetObject("ideology")) && ((*cand2)->safeGetObject("ideology"))) (*cand1)->setValue((*cand2)->safeGetObject("ideology")); 	  
	}
      }

      for (objiter rem = removalList.begin(); rem != removalList.end(); ++rem) {
	(*vp)->removeObject(*rem); 
      }
    }
  }

  // Per-state merging
  for (map<string, Object*>::iterator name = poptypes.begin(); name != poptypes.end(); ++name) {
    string popTypeName = (*name).first;
    if (popTypeName == "farmers")   continue;
    if (popTypeName == "labourers") continue;
    if (popTypeName == "slaves")    continue;
    if (popTypeName == "clergymen")    continue;

    for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
      objvec states = (*vic)->getValue("state");
      for (objiter state = states.begin(); state != states.end(); ++state) {
	Object* provList = (*state)->safeGetObject("provinces");
	if (!provList) continue;

	map<string, int> cultureSizes; 
	for (int i = 0; i < provList->numTokens(); ++i) {
	  string provTag = provList->getToken(i);
	  Object* vicProv = vicGame->safeGetObject(provTag);
	  if (!vicProv) continue;
	  objvec candidates = vicProv->getValue(popTypeName);
	  for (objiter cand = candidates.begin(); cand != candidates.end(); ++cand) {
	    cultureSizes[getPopCulture(*cand)] += (*cand)->safeGetInt("size"); 
	  }
	}

	for (int i = 0; i < provList->numTokens(); ++i) {
	  string provTag = provList->getToken(i);
	  Object* vicProv = vicGame->safeGetObject(provTag);
	  if (!vicProv) continue;
	  objvec candidates = vicProv->getValue(popTypeName);
	  objvec removeList; 
	  for (objiter cand = candidates.begin(); cand != candidates.end(); ++cand) {
	    int newSize = cultureSizes[getPopCulture(*cand)];
	    if (1 > newSize) {removeList.push_back(*cand); continue;}

	    if (newSize > 75000) newSize = 75000; 
	    (*cand)->resetLeaf("size", newSize);
	    cultureSizes[getPopCulture(*cand)] -= newSize; 
	  }

	  for (objiter rem = removeList.begin(); rem != removeList.end(); ++rem) {
	    vicProv->removeObject(*rem); 
	  }	  
	}
      }
    }
  }
  

  // Hand out money
  for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
    for (map<string, Object*>::iterator name = poptypes.begin(); name != poptypes.end(); ++name) {
      objvec currPops = (*prov)->getValue((*name).first);
      for (objiter pop = currPops.begin(); pop != currPops.end(); ++pop) {
	(*pop)->resetLeaf("money", (*pop)->safeGetFloat("size", 1) * (*name).second->safeGetFloat("money", 1));
	if (configObject->safeGetString("debugProvSize", "no") == "yes") {
	  if (((*name).first == "farmers") || ((*name).first == "labourers")) {
	    (*pop)->resetLeaf("money", 0);
	    (*pop)->resetLeaf("size", 4000000); 
	  }
	  //if ((*name).first == "slaves") (*pop)->setKey("artisans"); 
	}
      }
    }
  }

  // Assign workers to RGOs
  for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
    Object* rgo = (*prov)->safeGetObject("rgo");
    if (!rgo) continue;
    Object* emp = rgo->safeGetObject("employment");
    if (!emp) continue;
    emp->resetLeaf("province_id", (*prov)->getKey()); 
    Object* employees = new Object("employees");
    emp->setValue(employees);

    objvec empList;
    bool farmers = (*prov)->safeGetString("canHaveFarmers", "no") == "yes"; 
    objvec labs = (*prov)->getValue(farmers ? "farmers" : "labourers");
    for (objiter lab = labs.begin(); lab != labs.end(); ++lab) {
      Object* temp = new Object(Parser::ObjectListMarker);
      Object* popid = new Object("province_pop_id");
      temp->setValue(popid);
      popid->setLeaf("province_id", (*prov)->getKey());
      popid->setLeaf("index", (int) empList.size());
      popid->setLeaf("type", farmers ? 8 : 9);
      if (configObject->safeGetString("debugProvSize", "no") == "yes") {
	temp->setLeaf("count", rgo->safeGetString("vanillaCount", (*lab)->safeGetString("size")));
      }
      else
	temp->setLeaf("count", (*lab)->safeGetString("size"));
      empList.push_back(temp); 
    }
    employees->setValue(empList);
  }
  
}

void WorkerThread::moveCapitals () {
  for (map<Object*, objvec>::iterator vc = vicCountryToEu3CountriesMap.begin(); vc != vicCountryToEu3CountriesMap.end(); ++vc) {
    if (0 == (*vc).second.size()) continue;
    Object* eu3Prov = eu3Game->safeGetObject((*vc).second[0]->safeGetString("capital"));
    if (!eu3Prov) continue;
    if (0 == eu3ProvinceToVicProvincesMap[eu3Prov].size()) continue; 
    Object* vicProvince = eu3ProvinceToVicProvincesMap[eu3Prov][0];
    (*vc).first->resetLeaf("capital", vicProvince->getKey());
  }
}

void WorkerThread::convertCores () {
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    set<Object*> cores;
    objvec eu3s = vicProvinceToEu3ProvincesMap[*vp];
    for (objiter eu3 = eu3s.begin(); eu3 != eu3s.end(); ++eu3) {
      objvec currCores = (*eu3)->getValue("core");
      for (objiter cc = currCores.begin(); cc != currCores.end(); ++cc) {
	Object* vic = findVicCountryByEu3Country(findEu3CountryByTag((*cc)->getLeaf()));
	if (!vic) {
	  Logger::logStream(Logger::Error) << "Error: Could not find Vic country for EU3 tag "
					   << (*cc)->getLeaf()
					   << ", not assigning core.\n";
	  continue;
	}
	cores.insert(vic);
	
	Logger::logStream(Logger::Debug) << nameAndNumber(*vp)
					 << " is core of "
					 << (*cc)->getLeaf() << " and hence "
					 << (vic ? vic->getKey() : string("nothing")) << ", "
					 << (int) eu3s.size() << " "
					 << (*cc)->getLeaf()
					 << ".\n";
	
      }
    }

    /*
    Logger::logStream(Logger::Debug) << nameAndNumber(*vp)
				     << " is core of "
				     << (int) cores.size()
				     << " nations: ";
    */
    for (set<Object*>::iterator c = cores.begin(); c != cores.end(); ++c) {
      //Logger::logStream(Logger::Debug) << ((*c) ? (*c)->getKey() : string("NONE")) << " ";
      (*vp)->setLeaf("core", addQuotes((*c)->getKey())); 
    }
    //Logger::logStream(Logger::Debug) << "\n"; 
  }
}

double WorkerThread::partyModifier (string party, string area, string stance, 
				    Object* eu3Country,
				    Object* modifiers,
				    map<string, Object*>& otherParties) {

  if (0 == modifiers) return 1;
  if (0 == eu3Country) return 1;
  Object* modForParty = modifiers->safeGetObject(party);
  if (!modForParty) return 1;
  Object* modForStance = modForParty->safeGetObject(stance);
  if (!modForStance) return 1; 
  
  double ret = 1; 

  /* Example:
  socialist = {
    start_date = 1848.1.1
    laissez_faire = {
      conservative = {
	economic_policy = {
	  laissez_faire = 0.01
	}
      }
    }
    interventionism = { 
      liberal = {
	economic_policy = {
	  laissez_faire = 1.1
	} 
      }
    }
    jingoism = {
      ideas = {
	national_conscripts = 1.25  
      }
      sliders = {
	aristocracy_plutocracy = -0.03 
      }
    }
  }
  So, if conservatives are laissez_faire, socialist weight for that is multiplied by 0.01. 
  If liberals are laissez_faire, socialist weight for interventionism increases 10%.
  If the nation has National Conscripts, jingoism becomes 25% more likely.
  Each point towards aristocracy increases the weight by 3%. 
  */

  static int numParties = -1; 
  numParties++; 
  
  for (map<string, Object*>::iterator otherParty = otherParties.begin(); otherParty != otherParties.end(); ++otherParty) {
    Object* modifierFromOtherParty = modForStance->safeGetObject((*otherParty).first);
    if (!modifierFromOtherParty) continue;
    Object* modifiersForArea = modifierFromOtherParty->safeGetObject(area);
    if (!modifiersForArea) continue;
    string otherPartyStance = (*otherParty).second->safeGetString(area, "UMDUH");
    double specificModifier = modifiersForArea->safeGetFloat(otherPartyStance, -1);
    if (specificModifier < 0) continue;
    ret *= specificModifier;

    if (10 > numParties) Logger::logStream(Logger::Debug) << "Weight for "
							  << stance << " in "
							  << eu3Country->getKey() << " party " 
							  << party << " adjusted by "
							  << specificModifier 
							  << " due to "
							  << otherPartyStance 
							  << " of party "
							  << (*otherParty).first
							  << ".\n"; 
  }
  


  Object* national_idea_mods = modForStance->safeGetObject("ideas");
  if (national_idea_mods) {
    objvec nmods = national_idea_mods->getLeaves();
    for (objiter nmod = nmods.begin(); nmod != nmods.end(); ++nmod) {
      if (eu3Country->safeGetString((*nmod)->getKey(), "no") != "yes") continue;
      double specificModifier = national_idea_mods->safeGetFloat((*nmod)->getKey(), -1);
      if (specificModifier < 0) continue; 
      ret *= specificModifier;      
      if (10 > numParties) Logger::logStream(Logger::Debug) << "Weight for "
							    << stance << " in country "
							    << eu3Country->getKey() << " "
							    << party << " adjusted by "
							    << specificModifier 
							    << " due to idea "
							    << (*nmod)->getKey()
							    << ".\n"; 
    }
  }
  
  Object* slider_mods = modForStance->safeGetObject("sliders");
  if (slider_mods) {
    objvec smods = slider_mods->getLeaves();
    for (objiter smod = smods.begin(); smod != smods.end(); ++smod) {
      int position = eu3Country->safeGetInt((*smod)->getKey());
      if (0 == position) continue;
      double specificModifier = 1 + position * slider_mods->safeGetFloat((*smod)->getKey());
      ret *= specificModifier;      
      if (10 > numParties) Logger::logStream(Logger::Debug) << "Weight for "
							    << stance << " in "
							    << eu3Country->getKey() << " "
							    << party << " adjusted by "
							    << specificModifier 
							    << " due to slider "
							    << (*smod)->getKey() << " " 
							    << position 
							    << ".\n";
    }
  }
  return ret;
}

struct PartyAssignment {
  double score;
  Object* eu3Country;
  string vicTag; 
};

void WorkerThread::createParties () {
  ofstream writer;
  
  Object* ideologies = configObject->safeGetObject("ideologies");
  if (!ideologies) {
    Logger::logStream(Logger::Error) << "Error: Could not find ideology list in config.txt. Aborting party creation.\n";
    return; 
  }

  double humanBonus = ideologies->safeGetFloat("humanBonus"); 
  
  objvec tagdefs = ideologies->getLeaves(); // Objects defining resemblances for purposes of choosing which vanilla party-package to use. 
  map<string, Object*> referenceParties;
  for (map<string, Object*>::iterator p = tagToPartiesMap.begin(); p != tagToPartiesMap.end(); ++p) {
    if ((*p).first == "REB") continue; 
    referenceParties[(*p).first] = new Object((*p).second); 
  }

  map<Object*, Object*> countryToFileMap; 
  int partyId = 1;
  vector<PartyAssignment*> scores; 
  for (map<string, Object*>::iterator v = tagToPartiesMap.begin(); v != tagToPartiesMap.end(); ++v) {
    string vtag = (*v).first;
    if (vtag == "REB") continue; 
    Object* vicCountry = findVicCountryByTag(vtag);
    Object* partyList = (*v).second;
    if (!partyList) {
      Logger::logStream(Logger::Debug) << "Did not find parties for " << vtag << "\n"; 
      continue;
    }

    if (0 == vicCountryToEu3CountriesMap[vicCountry].size()) {
      partyId += partyList->getValue("party").size();
      continue; 
    }
    
    Object* eu3Country = vicCountryToEu3CountriesMap[vicCountry][0];
    string fileName("countries/");
    Object* currCustom = customObject->safeGetObject(eu3Country->getKey());
    
    fileName += vtag; 
    fileName += ".txt";
    countryPointers->resetLeaf(vicCountry->getKey(), addQuotes(fileName));
    vicCountry->unsetValue("active_party");
    vicCountry->unsetValue("ruling_party"); 
    
    Object* currCountryObject = new Object("someCountry");
    countryToFileMap[eu3Country] = currCountryObject; 
    Object* colours = partyList->safeGetObject("color");
    if (!colours) {
      colours = new Object("color");
      colours->setObjList(true);
      colours->addToList("111");
      colours->addToList("88");
      colours->addToList("87");
    }
    currCountryObject->setValue(colours);
    string graphs = partyList->safeGetString("graphical_culture", "EuropeanGC");
    currCountryObject->setLeaf("graphical_culture", graphs); 
  
    for (map<string, Object*>::iterator v2 = tagToPartiesMap.begin(); v2 != tagToPartiesMap.end(); ++v2) {
      if ((*v2).first == "REB") continue; 
      PartyAssignment* currScore = new PartyAssignment();
      scores.push_back(currScore);
      currScore->score = (eu3Country->safeGetString("human", "no") == "yes") ? humanBonus : 0; 
      currScore->eu3Country = eu3Country;
      currScore->vicTag = (*v2).first;

      double bid = 0; 
      if (currCustom) {
	bid = currCustom->safeGetFloat(currScore->vicTag);
	bid *= eu3Country->safeGetFloat("customPoints");
	bid *= ideologies->safeGetFloat("partyPointsPerCustom", 0.01); 
      }
      currScore->score += bid; 
      
      Object* comparisonObject = ideologies->safeGetObject(currScore->vicTag);
      if (!comparisonObject) continue;
	
      Object* ideaList = comparisonObject->getNeededObject("ideas");
      objvec ideas = ideaList->getLeaves();
      for (objiter idea = ideas.begin(); idea != ideas.end(); ++idea) {
	if (eu3Country->safeGetString((*idea)->getKey(), "no") != "yes") continue;
	currScore->score += atof((*idea)->getLeaf().c_str()); 
      }
      Object* sliderList = comparisonObject->getNeededObject("sliders");
      objvec sliders = sliderList->getLeaves();
      for (objiter slider = sliders.begin(); slider != sliders.end(); ++slider) {
	currScore->score += eu3Country->safeGetFloat((*slider)->getKey()) * atof((*slider)->getLeaf().c_str());
      }
      Object* modList = comparisonObject->getNeededObject("modifiers");
      objvec mods = modList->getLeaves();
      objvec cMods = eu3Country->getValue("modifier");
      map<string, bool> countryMods;
      for (objiter mod = cMods.begin(); mod != cMods.end(); ++mod) countryMods[remQuotes((*mod)->safeGetString("modifier"))] = true; 
      for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
	if (!countryMods[(*mod)->getKey()]) continue;
	currScore->score += atof((*mod)->getLeaf().c_str()); 
      }
      scores.push_back(currScore);
    }
  }

  sort(scores.begin(), scores.end(), deref<PartyAssignment>(member_gt(&PartyAssignment::score))); 
  map<string, bool> assignedVic;
  map<Object*, bool> assignedEu3; 
  for (vector<PartyAssignment*>::iterator pa = scores.begin(); pa != scores.end(); ++pa) {
    if (assignedVic[(*pa)->vicTag]) continue;
    if (assignedEu3[(*pa)->eu3Country]) continue;
    if (!referenceParties[(*pa)->vicTag]) continue;
    if ((*pa)->vicTag == "REB") continue; 
    assignedVic[(*pa)->vicTag] = true;
    assignedEu3[(*pa)->eu3Country] = true;
    
    Object* currCountryObject = countryToFileMap[(*pa)->eu3Country]; 
    Object* vicCountry = findVicCountryByEu3Country((*pa)->eu3Country);

    Logger::logStream(Logger::Debug) << (*pa)->eu3Country->getKey() << " has "
				     << (*pa)->score << " points for "
				     << (*pa)->vicTag << ", assigning parties.\n"; 
    
    objvec bestPartyList = referenceParties[(*pa)->vicTag]->getValue("party");
    for (objiter p = bestPartyList.begin(); p != bestPartyList.end(); ++p) {
      Object* party = new Object(*p);
      currCountryObject->setValue(party);
      //sprintf(strbuffer, "\"Party number %i\"", partyId); 
      //party->resetLeaf("name", strbuffer); 
      if (days(party->safeGetString("start_date")) <= days(remQuotes(vicGame->safeGetString("start_date", "\"1836.1.1\"")))) {
	if (vicCountry->safeGetString("ruling_party", "none") == "none") vicCountry->resetLeaf("ruling_party", partyId);
	vicCountry->setLeaf("active_party", partyId);	
      }
      partyId++; // Not actually listed in party object! Derived from position in file. 
    }    
  }


#ifdef BLAH
  objvec ideas = ideologies->getLeaves(); 
  vector<string> areas;
  areas.push_back("economic_policy");
  areas.push_back("trade_policy");
  areas.push_back("religious_policy");
  areas.push_back("citizenship_policy");
  areas.push_back("war_policy");
 
  map<string, map<string, map<string, double> > > ideologyToPositionsMap; // Party, area, then position.
  for (vector<pair<string, Object*> >::iterator v = tagToPartiesMap.begin(); v != tagToPartiesMap.end(); ++v) {
    Object* partyList = (*v).second; 
    if (!partyList) continue; 
    objvec parties = partyList->getValue("party");
    for (objiter p = parties.begin(); p != parties.end(); ++p) {
      string ideology = (*p)->safeGetString("ideology");
      for (vector<string>::iterator a = areas.begin(); a != areas.end(); ++a) {
	ideologyToPositionsMap[ideology][*a][(*p)->safeGetString(*a)]++;
	ideologyToPositionsMap[ideology][*a]["total"]++; 
      }
    }
  }

  // Calculate total number of modding points to distribute; it's equal to the amount it
  // would take to create all the vanilla parties. 
  double totalModPoints = 0;
  for (vector<pair<string, Object*> >::iterator v = tagToPartiesMap.begin(); v != tagToPartiesMap.end(); ++v) {
    Object* partyList = (*v).second; 
    if (!partyList) continue; 
    if (!partyList) continue; 
    objvec parties = partyList->getValue("party");
    for (objiter p = parties.begin(); p != parties.end(); ++p) {
      string ideology = (*p)->safeGetString("ideology");
      for (vector<string>::iterator a = areas.begin(); a != areas.end(); ++a) {
	totalModPoints -= log(ideologyToPositionsMap[ideology][*a][(*p)->safeGetString(*a)] / ideologyToPositionsMap[ideology][*a]["total"]);
      }
    }
  }
  
  Logger::logStream(Logger::Game) << "Total modding points: " << totalModPoints << "\n";
  Logger::logStream(Logger::Game) << "Ideology distributions: \n"; 
  for (objiter i = ideas.begin(); i != ideas.end(); ++i) {
    string curr = (*i)->getKey(); 
    Logger::logStream(Logger::Game) << curr << " : \n";
    for (map<string, map<string, double> >::iterator j = ideologyToPositionsMap[curr].begin(); j != ideologyToPositionsMap[curr].end(); ++j) {
      Logger::logStream(Logger::Game) << "  " << (*j).first << " : \n";
      for (map<string, double>::iterator k = (*j).second.begin(); k != (*j).second.end(); ++k) {
	Logger::logStream(Logger::Game) << "    " << (*k).first << " : " << (*k).second << "\n";
      }
    }
  }

  map<Object*, Object*> countryToFileMap; 
  int partyId = 1;
  //for (map<Object*, objvec>::iterator v = vicCountryToEu3CountriesMap.begin(); v != vicCountryToEu3CountriesMap.end(); ++v) {
  for (vector<pair<string, Object*> >::iterator v = tagToPartiesMap.begin(); v != tagToPartiesMap.end(); ++v) {
    string vtag = (*v).first;
    if (vtag == "REB") continue; 
    Object* vicCountry = findVicCountryByTag(vtag);
    Object* partyList = (*v).second;
    if (!partyList) {
      Logger::logStream(Logger::Debug) << "Did not find parties for " << vtag << "\n"; 
      continue;
    }

    if (0 == vicCountryToEu3CountriesMap[vicCountry].size()) {
      partyId += partyList->getValue("party").size();
      continue; 
    }
    
    Object* eu3Country = vicCountryToEu3CountriesMap[vicCountry][0];
    string fileName("countries/");
    
    fileName += vtag; 
    fileName += ".txt";
    countryPointers->resetLeaf(vicCountry->getKey(), addQuotes(fileName));
    vicCountry->unsetValue("active_party");
    vicCountry->unsetValue("ruling_party"); 
    
    Object* currCountryObject = new Object("someCountry");
    countryToFileMap[eu3Country] = currCountryObject; 
    Object* colours = partyList->safeGetObject("color");
    if (!colours) {
      colours = new Object("color");
      colours->setObjList(true);
      colours->addToList("111");
      colours->addToList("88");
      colours->addToList("87");
    }
    currCountryObject->setValue(colours);
    string graphs = partyList->safeGetString("graphical_culture", "EuropeanGC");
    currCountryObject->setLeaf("graphical_culture", graphs); 

    map<string, Object*> localPartiesMap; 
    for (objiter idea = ideas.begin(); idea != ideas.end(); ++idea) {
      Object* party = new Object("party");
      currCountryObject->setValue(party);
      party->setLeaf("name", addQuotes(vtag + "_" + (*idea)->getKey()));
      party->setLeaf("start_date", (*idea)->safeGetString("start_date", "1830.1.1"));
      party->setLeaf("end_date", "2000.1.1");
      party->setLeaf("ideology", (*idea)->getKey());
      localPartiesMap[(*idea)->getKey()] = party; 

      for (vector<string>::iterator a = areas.begin(); a != areas.end(); ++a) {
	map<string, double> weights;
	for (map<string, double>::iterator h = ideologyToPositionsMap[(*idea)->getKey()][*a].begin(); h != ideologyToPositionsMap[(*idea)->getKey()][*a].end(); ++h) {
	  weights[(*h).first] = (*h).second * partyModifier((*idea)->getKey(), (*a), (*h).first, eu3Country, ideologies, localPartiesMap);
	}
	
	double totalWeights = 0; 
	for (map<string, double>::iterator w = weights.begin(); w != weights.end(); ++w) {
	  if ((*w).first == "total") continue;
	  totalWeights += (*w).second;
	}
	
	double choice = rand();
	choice /= RAND_MAX;
	choice *= totalWeights;
	totalWeights = 0; 
	for (map<string, double>::iterator w = weights.begin(); w != weights.end(); ++w) {
	  if ((*w).first == "total") continue;
	  totalWeights += (*w).second;
	  if (choice > totalWeights) continue;
	  party->setLeaf((*a), (*w).first); 
	  break; 
	}
      }

      //party->resetLeaf("partyid", partyId); 
      if (days(party->safeGetString("start_date")) <= days(remQuotes(vicGame->safeGetString("start_date", "\"1836.1.1\"")))) {
	if ((*idea)->safeGetString("start_ruling", "no") == "yes") vicCountry->setLeaf("ruling_party", partyId);
	vicCountry->setLeaf("active_party", partyId);	
      }
      partyId++;
    }
  }
 
  if (customObject) {
   
    // Auction for party customisations. In each iteration, the country with the most
    // remaining modding points gets to make the highest-priority customisation it can
    // afford. If it can't afford anything, it is removed from the customisation list.
    // The process continues until all customisations have been made or nobody can afford
    // any more customisations.

    Object* extraGoods = configObject->safeGetObject("extraGoods");
    if (!extraGoods) {
      extraGoods = new Object("extraGoods");
      configObject->setValue(extraGoods); 
    }

    
    // Calculate modding points from government buildings
    for (objiter prov = eu3Provinces.begin(); prov != eu3Provinces.end(); ++prov) {
      Object* owner = findEu3CountryByTag((*prov)->safeGetString("owner"));
      if (!owner) continue;
      owner->resetLeaf("provsForModding", 1 + owner->safeGetInt("provsForModding")); 
      for (objiter b = buildingTypes.begin(); b != buildingTypes.end(); ++b) {
	int customPoints = (*b)->safeGetInt("customPoints");
	if (0 == customPoints) continue;
	if (!hasBuildingOrBetter((*b)->getKey(), (*prov))) continue;
	owner->resetLeaf("customPoints", owner->safeGetInt("customPoints") + customPoints);
      }
    }

    objvec customs = customObject->getLeaves();
    objvec bidders;
    double totalBidderPoints = 0; 
    for (objiter custom = customs.begin(); custom != customs.end(); ++custom) {
      string eu3Tag = (*custom)->getKey(); 
      Object* eu3Country = eu3TagToEu3CountryMap[eu3Tag];
      if (!eu3Country) continue;
      if (((*custom)->getLeaves().size() == 1) && ((*custom)->safeGetObject("research"))) continue; 
      bidders.push_back(eu3Country);
      double currFrac = eu3Country->safeGetFloat("customPoints") / max(1, eu3Country->safeGetInt("provsForModding"));
      eu3Country->resetLeaf("moddingFraction", currFrac);
      totalBidderPoints += currFrac; 
    }

    totalModPoints *= configObject->safeGetFloat("modPointMultiplier", 1); 
    
    for (objiter b = bidders.begin(); b != bidders.end(); ++b) {
      (*b)->resetLeaf("customPoints", (totalModPoints / totalBidderPoints) * (*b)->safeGetFloat("moddingFraction")); 
      Logger::logStream(Logger::Game) << "EU3 nation "
				      << (*b)->getKey()
				      << " entering party bidding with "
				      << (*b)->safeGetString("customPoints")
				      << " points.\n"; 
    }
    
    ObjectAscendingSorter pointSorter("customPoints"); 
    while (0 < bidders.size()) {
      sort(bidders.begin(), bidders.end(), pointSorter);
      Object* highestBidder = bidders.back();
      Object* fileObject = countryToFileMap[highestBidder];
      if (!fileObject) {
	// This should never happen.
	Logger::logStream(Logger::Error) << "Error: Could not find parties for EU3 country "
					 << highestBidder->getKey()
					 << ", removing from customisation list.\n";
	bidders.pop_back();
	continue; 
      }
      
      objvec cParties = fileObject->getLeaves();
      map<string, Object*> pMap; 
      for (objiter p = cParties.begin(); p != cParties.end(); ++p) {
	string id = (*p)->safeGetString("ideology", "NONE");
	if (id == "NONE") continue;
	pMap[id] = (*p); 
      }
      
      double modPoints = highestBidder->safeGetFloat("customPoints");
      Object* bidObject = customObject->safeGetObject(highestBidder->getKey());
      assert(bidObject); 
      objvec bids = bidObject->getLeaves();
      if ((0 == bids.size()) || (1 == bids.size() && bids[0]->getKey() == "research")) {
	// Ran out of bids, this is ok.
	Logger::logStream(Logger::Game) << "EU3 country "
					<< highestBidder->getKey()
					<< " has no more bids, reserve "
					<< modPoints
					<< ".\n"; 
	bidders.pop_back();
	continue; 
      }

      Object* vicCountry = findVicCountryByEu3Country(highestBidder);
      assert(vicCountry); 
      
      for (objiter bid = bids.begin(); bid != bids.end(); ++bid) {
	if ((*bid)->getKey() == "research") continue; // Save for later. 
	
	// Whether it succeeds or not, it's gone. 
	bidObject->removeObject(*bid);

	int priorBids = highestBidder->safeGetInt("numSuccessfulBids") + 1;	
	string idToChange = (*bid)->getKey();
	if (idToChange == "resource") {
	  string which = (*bid)->safeGetString("which"); 
	  double existing = goodsToRedistribute[which];
	  if (1 > existing) existing = 1;
	  double cost = existing / goodsToRedistribute["total"];
	  cost = -log(cost);
	  double amount = (*bid)->safeGetFloat("amount");
	  cost *= 0.1; 
	  cost *= amount;
	  cost *= sqrt(priorBids);
	  if (cost < priorBids) cost = priorBids; 
	  if (cost > modPoints) {
	    double remainder = (*bid)->safeGetFloat("useRemainder", -1);
	    if (remainder > 0) {
	      cost /= amount; // This is cost per unit
	      amount = (modPoints * remainder / cost); // New number of units to buy
	      cost *= amount;  // New total cost 
	    }
	    else {
	      Logger::logStream(Logger::Game) << "EU3 nation "
					      << highestBidder->getKey()
					      << " does not have enough points to buy "
					      << amount << " "
					      << which
					      << " which costs "
					      << cost
					      << ".\n";
	      continue;
	    }
	  }
	  modPoints -= cost; 
	  highestBidder->resetLeaf("customPoints", modPoints);				   
	  highestBidder->resetLeaf("numSuccessfulBids", priorBids);	  
	  Logger::logStream(Logger::Game) << "EU3 nation "
					  << highestBidder->getKey()
					  << " buys "
					  << amount << " "
					  << which 
					  << " at price "
					  << cost
					  << ".\n";
	  extraGoods->resetLeaf(which, extraGoods->safeGetFloat(which) + 0.5*amount);
	  Object* stockpile = vicCountry->safeGetObject("stockpile");
	  if (!stockpile) {
	    stockpile = new Object("stockpile");
	    vicCountry->setValue(stockpile); 
	  }
	  stockpile->resetLeaf(which, amount + stockpile->safeGetFloat(which));
	  break; 
	}
	else if (idToChange == "nationalValue") {
	  string current = vicCountry->safeGetString("nationalvalue");
	  if (current == (*bid)->getLeaf()) {
	    Logger::logStream(Logger::Game) << "EU3 nation "
					    << highestBidder->getKey()
					    << " already has national value "
					    << current 
					    << ".\n";
	    continue; 
	  }
	  double curCost = vicCountry->safeGetFloat(remQuotes(current), 1);
	  double bidCost = vicCountry->safeGetFloat((*bid)->getLeaf(), 1);
	  if (bidCost < 0) {
	    curCost -= bidCost;
	    bidCost = 0.01; 
	  }
	  double cost = log(curCost) - log(bidCost);
	  cost *= sqrt(priorBids);
	  if (cost < priorBids) cost = priorBids;
	  if (cost > modPoints) {
	    Logger::logStream(Logger::Game) << "EU3 nation "
					    << highestBidder->getKey()
					    << " cannot afford national value "
					    << (*bid)->getLeaf()
					    << ".\n";
	    continue; 
	  }

	  Logger::logStream(Logger::Game) << "EU3 nation "
					  << highestBidder->getKey()
					  << " buys national value "
					  << (*bid)->getLeaf()
					  << " at cost "
					  << cost 
					  << ".\n";
	  
	  modPoints -= cost; 
	  highestBidder->resetLeaf("customPoints", modPoints);				   
	  highestBidder->resetLeaf("numSuccessfulBids", priorBids);
	  vicCountry->resetLeaf("nationalvalue", addQuotes((*bid)->getLeaf()));
	  break; 
	}
	
	Object* partyToChange = pMap[idToChange];
	if (!partyToChange) {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find party "
					     << idToChange
					     << " for EU3 nation "
					     << highestBidder->getKey()
					     << ".\n"; 
	  continue; 
	}

	string area = (*bid)->safeGetString("area", "NONE");
	if (area == "NONE") {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find area to change in bid "
					     << (*bid) 
					     << " for EU3 nation "
					     << highestBidder->getKey()
					     << ".\n"; 
	  continue; 
	}
	if (find(areas.begin(), areas.end(), area) == areas.end()) {
	  Logger::logStream(Logger::Warning) << "Warning: Area "
					     << area
					     << " in bid "
					     << (*bid) 
					     << " for EU3 nation "
					     << highestBidder->getKey()
					     << " is not valid.\n"; 
	  continue; 
	}
	
	string position = (*bid)->safeGetString("position", "NONE");
	if (position == "NONE") {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find position for area "
					     << area 
					     << " in bid "
					     << (*bid) 
					     << " for EU3 nation "
					     << highestBidder->getKey()
					     << ".\n"; 
	  continue; 
	}
	map<string, map<string, double> >::iterator areaMap = ideologyToPositionsMap[(*bid)->getKey()].find(area);
        if (areaMap == ideologyToPositionsMap[(*bid)->getKey()].end()) {
	  Logger::logStream(Logger::Warning) << "Warning: Position "
					     << position << " in area "
					     << area 
					     << " in bid "
					     << (*bid) 
					     << " for EU3 nation "
					     << highestBidder->getKey()
					     << " does not seem to be valid.\n"; 
	  continue; 
	}

	string oldPosition = partyToChange->safeGetString(area);
	
	if (position == oldPosition) {
	  Logger::logStream(Logger::Game) << idToChange 
					  << " already has "
					  << position << " in area "
					  << area 
					  << " for EU3 nation "
					  << highestBidder->getKey()
					  << ", ignoring bid.\n"; 	  
	  continue;
	}
	if ((*bid)->safeGetString("unless", "BAHNOTHING") == oldPosition) { 
	  Logger::logStream(Logger::Game) << idToChange 
					  << " has "
					  << (*bid)->safeGetString("unless", "BAHNOTHING") << " in area "
					  << area 
					  << " for EU3 nation "
					  << highestBidder->getKey() 
					  << ", ignoring bid due to unless clause.\n"; 	  
	  continue;
	}

	
	
	double currentWeight = ideologyToPositionsMap[idToChange][area][partyToChange->safeGetString(area)]*partyModifier(idToChange,
															  area,
															  oldPosition,
															  highestBidder,
															  ideologies,
															  pMap);
	double changedWeight = ideologyToPositionsMap[idToChange][area][position]*partyModifier(idToChange,
												area,
												position,
												highestBidder,
												ideologies,
												pMap);
	if (currentWeight < 0.01) currentWeight = 0.01;
	changedWeight /= currentWeight;
	if (changedWeight < 0.001) changedWeight = 0.001;
	double cost = -log(changedWeight) * sqrt(priorBids);
	if (cost < priorBids) cost = priorBids; 
	if (modPoints < cost) {
	  Logger::logStream(Logger::Game) << "EU3 nation "
					  << highestBidder->getKey()
					  << " does not have enough points for modification "
					  << (*bid)
					  << ", which costs "
					  << cost
					  << " and reserve is "
					  << modPoints
					  << ".\n";
	  continue; 
	}
	modPoints -= cost;
	highestBidder->resetLeaf("customPoints", modPoints);
	partyToChange->resetLeaf(area, position);
	Logger::logStream(Logger::Game) << "Party " << idToChange
					<< " for nation "
					<< highestBidder->getKey()
					<< " reset to "
					<< area << " = " << position
					<< " at cost "
					<< cost
					<< " leaving reserve "
					<< highestBidder->safeGetString("customPoints")
					<< ".\n";
	highestBidder->resetLeaf("numSuccessfulBids", priorBids);
	break; 
      }
    }
  }

#endif
	 
  for (map<Object*, objvec>::iterator v = vicCountryToEu3CountriesMap.begin(); v != vicCountryToEu3CountriesMap.end(); ++v) {
    Object* vicCountry = (*v).first;
    if (0 == (*v).second.size()) continue;
    if (vicCountry->getKey() == "REB") continue; 
    Object* currCountryObject = countryToFileMap[(*v).second[0]];
    if (!currCountryObject) {
      Logger::logStream(Logger::Error) << "Error: Could not find file for Vic country "
				       << vicCountry->getKey()
				       << "\n";
      continue; 
    }

    string fileName("./Output/common/countries/");
    fileName += vicCountry->getKey(); 
    fileName += ".txt";
    
    Parser::topLevel = currCountryObject;
    writer.open(fileName.c_str());
    writer << (*currCountryObject);
    writer.close(); 
  }

  writer.open("./Output/common/countries.txt");
  Parser::topLevel = countryPointers;
  writer << (*countryPointers); 
  writer.close();
}

void WorkerThread::convert () {
  if (!eu3Game) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  objvec dbprovs = configObject->getValue("debugPop");
  for (objiter db = dbprovs.begin(); db != dbprovs.end(); ++db) {
    Object* eu3prov = eu3Game->safeGetObject((*db)->getLeaf());
    if (eu3prov) eu3prov->resetLeaf("debugPop", "yes");
  }
  dbprovs = configObject->getValue("debugLiteracy");
  for (objiter db = dbprovs.begin(); db != dbprovs.end(); ++db) {
    Object* eu3prov = eu3Game->safeGetObject((*db)->getLeaf());
    if (eu3prov) eu3prov->resetLeaf("debugLiteracy", "yes");
  }
  dbprovs = configObject->getValue("debugColony");
  for (objiter db = dbprovs.begin(); db != dbprovs.end(); ++db) {
    Object* eu3prov = eu3Game->safeGetObject((*db)->getLeaf());
    if (eu3prov) eu3prov->resetLeaf("debugColony", "yes");
  }

  
  Logger::logStream(Logger::Game) << "Loading Vicky source file.\n";
  vicGame = loadTextFile(targetVersion + "input.v2");
  Logger::logStream(Logger::Game) << "Done loading.\n";
  vicGame->unsetValue("active_war");
  Object* combats = vicGame->safeGetObject("combat");
  combats->clear();
  initialise(); 
  createCountryMappings();
  clearVicOwners(); 
  createProvinceMappings();
  moveCapitals();
  calculateCustomPoints(); 
  reassignProvinces();
  convertCores();  
  moveRgos(); 
  moveFactories(); 
  movePops();
  mergePops();
  convertArmies();
  diplomacy();
  calculateTradePower(); 
  techLevels(); 
  navalBases();
  nationalValues();   
  createParties();
  leaders(); 
  redistributeGoods(); 
  
  Object* oldHistory = loadTextFile(targetVersion + "history\\pops\\pophistory.txt"); 

  ofstream writer;
  objvec vicObjects = vicGame->getLeaves(); 
  for (objiter vp = vicObjects.begin(); vp != vicObjects.end(); ++vp) {
    (*vp)->unsetValue("factory");
    (*vp)->unsetValue("continent");
    (*vp)->unsetValue("canHaveFarmers");

    Object* focus = (*vp)->safeGetObject("national_focus");
    if (focus) focus->clear(); 
    
    // RGO must be defined after POPs
    Object* rgo = (*vp)->safeGetObject("rgo");
    if (!rgo) continue;
    rgo->unsetValue("vanillaCount"); 
    (*vp)->unsetValue("rgo"); 
    (*vp)->setValue(rgo);

    objvec pops = (*vp)->getLeaves();
    map<string, int> sizes;
    for (objiter p = pops.begin(); p != pops.end(); ++p) {
      Object* issues = (*p)->safeGetObject("issues");
      if (issues) issues->unsetValue("size");
      issues = (*p)->safeGetObject("ideology");
      if (issues) issues->unsetValue("size");
      
      int curr = (*p)->safeGetInt("size");
      if (0 == curr) continue;
      sizes[(*p)->getKey()] += curr;
    }

    Object* checkOld = oldHistory->safeGetObject((*vp)->getKey());
    if (!checkOld) {
      Logger::logStream(Logger::Debug) << "Problem: Province \""
				       << (*vp)->getKey()
				       << "\" doesn't seem to have a pop history.\n";
      checkOld = new Object((*vp)->getKey());
      oldHistory->setValue(checkOld);
    }

    objvec oldLeaves = checkOld->getLeaves();
    for (objiter old = oldLeaves.begin(); old != oldLeaves.end(); ++old) {
      (*old)->resetLeaf("size", "100"); 
    }
    
    for (map<string, int>::iterator i = sizes.begin(); i != sizes.end(); ++i) {
      Object* currPopType = checkOld->safeGetObject((*i).first);
      if (currPopType) currPopType->resetLeaf("size", (*i).second);
      else {
	currPopType = new Object((*i).first);
	currPopType->resetLeaf("culture", checkOld->getLeaves()[0]->safeGetString("culture"));
	currPopType->resetLeaf("religion", checkOld->getLeaves()[0]->safeGetString("religion"));
	currPopType->resetLeaf("size", (*i).second * configObject->safeGetFloat("rgoSizeModifier", 1));
	checkOld->setValue(currPopType);   	  
      }
    }

    string historyFile = remQuotes((*vp)->safeGetString("name")); 
    Object* original = 0;

    string filename = "history\\provinces\\" + historyFile + ".txt";
    
    if ((targetVersion == ".\\AHD\\") || (targetVersion == ".\\HoD\\")) {
      for (objiter currDirectory = provdirs.begin(); currDirectory != provdirs.end(); ++currDirectory) {
	filename = "history\\provinces\\" + remQuotes((*currDirectory)->getLeaf()) + "\\" + historyFile + ".txt";
	ifstream checkFile((targetVersion + filename).c_str());
	if (!checkFile) continue;
	original = loadTextFile(targetVersion + filename);
	break;
      }
    }
    else original = loadTextFile(targetVersion + filename); 
    if (!original) continue; 
    string owner = remQuotes((*vp)->safeGetString("owner", "\"NONE\""));
    if (owner != "NONE") {
      original->resetLeaf("owner", owner);
      original->resetLeaf("controller", owner);
    }
    string outputfile(".\\Output\\");
    outputfile += filename; 
    writer.open(outputfile.c_str()); 
    Parser::topLevel = original;
    writer << (*original);
    writer.close(); 
  }
 
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* eu3 = findEu3CountryByVicCountry(*vic);
    (*vic)->unsetValue("industrialStrength");
    (*vic)->unsetValue("numFactories");
    (*vic)->unsetValue("numUnits");
    (*vic)->unsetValue("powerScore");
    (*vic)->resetLeaf("last_election", "\"1832.3.3\"");
    (*vic)->resetLeaf("revanchism", "0.000");
    (*vic)->resetLeaf("diplomatic_points", eu3->safeGetString("diplomats", "0.000")); 
    (*vic)->resetLeaf("badboy", eu3->safeGetString("badboy", "0.000")); 
    for (objiter v = natValues.begin(); v != natValues.end(); ++v) {
      (*vic)->unsetValue(remQuotes((*v)->safeGetString("name")));
    }
    Object* active = (*vic)->safeGetObject("active_inventions");
    if (active) active->clear(); 

    
    double plurality = 1;
    Object* plur = configObject->safeGetObject("plurality");
    if (plur) {
      objvec leaves = plur->getLeaves();
      for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
	if (eu3->safeGetString((*leaf)->getKey(), "no") == "yes") plurality += plur->safeGetFloat((*leaf)->getKey());
	else plurality += plur->safeGetFloat((*leaf)->getKey()) * eu3->safeGetInt((*leaf)->getKey()); 
      }
    }
    (*vic)->resetLeaf("plurality", plurality);
    
    double effectiveEu3Money = eu3->safeGetFloat("treasury", 0);
    effectiveEu3Money -= 12*eu3->safeGetFloat("inflation")*eu3->safeGetFloat("estimated_monthly_income");
    objvec eu3loans = eu3->getValue("loan");
    for (objiter l = eu3loans.begin(); l != eu3loans.end(); ++l) {
      double amount = (*l)->safeGetFloat("amount");
      if ((*l)->safeGetString("lender", "\"---\"") == "\"---\"") effectiveEu3Money -= amount;
      else {
	Object* loan = new Object("creditor");
	loan->setLeaf("country", findVicTagFromEu3Tag((*l)->safeGetString("lender")));
	loan->setLeaf("interest", 0.01*(*l)->safeGetFloat("interest"));
	loan->setLeaf("debt", 5*amount); 
	loan->setLeaf("was_paid", "yes");
	(*vic)->setValue(loan); 
      }
    }
    
    effectiveEu3Money *= 5;
    Object* currCustom = customObject->safeGetObject(eu3->getKey(), customObject->getNeededObject("DUMMY"));
    effectiveEu3Money += eu3->safeGetFloat("customPoints")*currCustom->safeGetFloat("money")*configObject->safeGetFloat("", 100); 
    if (effectiveEu3Money < 0) {
      Object* loan = new Object("creditor");
      loan->setLeaf("country", "\"---\"");
      loan->setLeaf("interest", "0.02");
      loan->setLeaf("debt", -1.1*effectiveEu3Money);
      loan->setLeaf("was_paid", "yes");
      (*vic)->setValue(loan); 
      effectiveEu3Money *= -0.1; 
    }
    (*vic)->resetLeaf("money", effectiveEu3Money);
    
    objvec states = (*vic)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec facs = (*state)->getValue("state_buildings");
      if (0 == facs.size()) continue;
      Object* provs = (*state)->safeGetObject("provinces");
      if (!provs) continue;

      int totalLevels = 0; 
      for (objiter f = facs.begin(); f != facs.end(); ++f) {
	totalLevels += (*f)->safeGetInt("level"); 
      }

      int totalWorkers = 0; 
      for (int i = 0; i < provs->numTokens(); ++i) {
	string provnum = provs->getToken(i);
	Object* v2prov = vicGame->safeGetObject(provnum);
	if (!v2prov) continue;
	objvec craftsmen = v2prov->getValue("craftsmen");
	for (objiter c = craftsmen.begin(); c != craftsmen.end(); ++c) totalWorkers += (*c)->safeGetInt("size");
	craftsmen = v2prov->getValue("clerks");
	for (objiter c = craftsmen.begin(); c != craftsmen.end(); ++c) totalWorkers += (*c)->safeGetInt("size");
      }
      int requiredLevels = 1 + (totalWorkers / 10000);
      requiredLevels -= totalLevels;
      if (requiredLevels <= 0) continue;
      facs[0]->resetLeaf("level", requiredLevels + facs[0]->safeGetInt("level")); 
    }
  }
  
  Logger::logStream(Logger::Game) << "Done with conversion, writing to file.\n"; 
  writer.open(".\\Output\\converted.v2");
  Parser::topLevel = vicGame;
  writer << (*vicGame);
  writer.close();

  writer.open(".\\Output\\history\\pops\\1836.1.1\\all.txt");
  Parser::topLevel = oldHistory;
  writer << (*oldHistory);
  writer.close(); 
  Logger::logStream(Logger::Game) << "Done writing.\n";  
}

