#ifndef CLEANERWINDOW_HH
#define CLEANERWINDOW_HH

#include <QtGui>
#include <QObject>
#include <QThread> 
#include "Object.hh"
#include <map> 
#include <set> 

using namespace std; 

class WorkerThread; 

class CleanerWindow : public QMainWindow {
  Q_OBJECT
  
public:
  CleanerWindow (QWidget* parent = 0); 
  ~CleanerWindow ();
  
  QPlainTextEdit* textWindow; 
  WorkerThread* worker;

  void loadFile (string fname, int autoTask = -1);
		      
public slots:

  void cleanFile ();
  void convert ();  
  void getStats ();
  void loadFile ();   
  void messWithSize (); 
  void message (QString m);
  void simplify (); 
  void tradeStats (); 
  
private:

};

double days (string datestring, int* getyear = 0);
struct DateAscendingSorter {
  bool operator() (Object* one, Object* two) {return days(one->getKey()) < days(two->getKey());} 
};

struct PopSourceInfo {
  map<string, double> manpower;
  //double citysize;
  //double basetax;
  map<string, double> goods;
  pair<double, double> occupation;  
  pair<double, double> religion;
  pair<double, double> buildings;
  double totalMP;
  double totalPR; 
  double final;
  string tag; 
  
  void add (PopSourceInfo* dat);

  bool operator () (PopSourceInfo* one, PopSourceInfo* two) {return one->final > two->final;} // Greater-than for descending sort. 
};

struct BattleInfo {
  BattleInfo () : losses(0), killed(0) {}
  ~BattleInfo () {}

  int addBattle (Object* ourSide, Object* theirSide, int year);


  int losses;
  int killed;
  double ratio; 
  map<string, int> killMap;
  map<int, int> yearlyLosses; 
  
  static map<int, int> globalYearlyLosses; 
};


struct ObjectSorter {
  ObjectSorter (string k) {keyword = k;} 
  string keyword;
};
struct ObjectAscendingSorter : public ObjectSorter {
public:
  ObjectAscendingSorter (string k) : ObjectSorter(k) {}
  bool operator() (Object* one, Object* two) {return (one->safeGetFloat(keyword) < two->safeGetFloat(keyword));} 
private:
};
struct ObjectDescendingSorter : public ObjectSorter {
public:
  ObjectDescendingSorter (string k) : ObjectSorter(k) {}
  bool operator() (Object* one, Object* two) {return (one->safeGetFloat(keyword) > two->safeGetFloat(keyword));} 
private:
};

struct History {
  int numDays; 
  string religion;
  string culture;
  int productionTech;
  map<string, int> sliders;
  objvec modifiers;
  objvec triggerMods;
  Object* capital;
  string date; 
};

struct NationHistory : public History {
  // History does not store slider moves or cultures. 
  string government;
  int numWars;
};

struct ProvinceHistory : public History {
  string ownerTag;
  string controllerTag;
  set<string> cores; 
  map<string, bool> buildings;
  string ownerReligion;
  string ownerGovernment; 
  bool ownerAtWar;
  double citysize;
  double manpower;
  double basetax;
}; 

string addQuotes (string tag);
string remQuotes (string tag);

class WorkerThread : public QThread {
  Q_OBJECT
public:
  WorkerThread (string fname, int aTask = -1); 
  ~WorkerThread ();

  enum TaskType {LoadFile = 0, CleanFile, Statistics, Convert, MessWithSize, Simplify, TradeStats, NumTasks}; 
  void setTask(TaskType t) {task = t;} 

  static int PopDebug; 
  
protected:
  void run (); 
  
private:
  // Helper classes
  struct PopInfo {
    PopInfo (string p); 
    double worldTotal;
    double integratedLiteracy; 
    string ptype;
  };
  
  class ProvinceGroup {
    friend class WorkerThread; 
  public:
    ProvinceGroup (); 
    void addToGroup (string s);
    void addEu3Province (Object* prov);
    void addVicProvince (Object* vicprov, Object* eu3prov);
    void redistribute (int& popid);
    void moveLiteracy (); 
    static ProvinceGroup* getGroup (string s) {return allGroups[s];} 
    void printLiteracy ();
    
  private:
    objvec eu3Provs;
    objvec vicProvs;
    vector<WorkerThread::PopInfo*> popinfos;
    map<string, objvec> poptypeToIssuesMap;
    map<string, objvec> poptypeToIdeologyMap;
    map<string, double> poptypeToMaxSizeMap; 
    
    static map<string, ProvinceGroup*> allGroups;
    static WorkerThread* boss; 
  }; 
 
  // Misc globals 
  string targetVersion;
  string sourceVersion;   
  string fname; 
  Object* eu3Game;
  Object* vicGame;
  TaskType task; 
  Object* configObject;
  Object* customObject; 
  vector<WorkerThread::ProvinceGroup*> provinceGroups; 
  map<string, Object*> poptypes;
  double histPopFraction;
  Object* cachedPopWeights;
  map<string, double> regimentRatio; 
  int autoTask; 
  objvec natValues; 
  
  // Infrastructure 
  void loadFile (string fname); 
  void cleanFile ();
  void getStatistics ();
  void convert (); 
  void configure ();
  void messWithSize ();
  void simplify (); 
  void tradeStats (); 

  // Helper methods
  void addFactory (Object* vicNation, Object* factory);
  double calculateLiteracy (string poptype, Object* eu3prov);
  Object* convertDipObject (Object* edip); 
  Object* findEu3CountryByTag (string tag);
  Object* findVicCountryByTag (string tag);
  Object* findVicCountryByEu3Country (Object* eu3);
  Object* findEu3CountryByVicCountry (Object* vic);
  string findVicTagFromEu3Tag (string eu3tag, bool quotes = false);
  bool redistPops ();
  double getPopWeight (Object* eu3prov, string poptype);
  Object* getEu3ProvInfoFromEu3Province (Object* eu3prov);
  Object* getVicProvInfoFromVicProvince (Object* vicprov);
  string getReligionAtDate (Object* eun, string date);
  string nameAndNumber (Object* eu3prov); 
  bool heuristicIsCoastal (Object* eu3prov);
  bool heuristicVicIsCoastal (Object* vicprov);
  bool isCoreAtDate(Object* eu3prov, string ownertag, string date);
  bool isOverseasEu3Provinces (Object* oneProv, Object* twoProv);
  bool isOverseasVicProvinces (Object* oneProv, Object* twoProv); 
  string eu3CultureToVicCulture (string cult);
  void clearVicOwners (); 
  void resetCulture (Object* pop, Object* eu3prov, string& minority, bool needsClear = false);
  void initialise ();
  bool hasBuildingOrBetter (string building, Object* dat);
  void moveRgosWithList ();     
  int getTechLevel (Object* eu3country, string techtype);  
  double calculateReligionBadness(string provReligion, string stateReligion, string government);
  Object* addUnitToHigher (string vickey, string victype, int& numRegiments, Object* army, Object* vicCountry, string eu3Name, double vicStr);
  void createRegiment (int& number, string victype, int& numRegiments, Object* army, Object* vicCountry, string eu3Name);
  bool countrySatisfied (Object* mod, ProvinceHistory* period, int numDays); 
  bool isSatisfied (Object* mod, ProvinceHistory* period, Object* eu3prov, int numDays);   
  double demandModifier (Object* mod, ProvinceHistory* period, Object* eu3prov, int numDays);
  void printPopInfo (PopSourceInfo* dat, bool firstHalf);
  void interpolateInHistory (objvec& history, string keyword, int initial, int final, double exponent); 
  Object* loadTextFile (string fname);
  int category (Object* building);
  int level (Object* building);
  string getPopCulture (Object* pop);
  double partyModifier (string party, string area, string stance, 
			Object* eu3Country,
			Object* modifiers,
			map<string, Object*>& otherParties);
  
  // Inputs 
  void createProvinceMappings ();
  void createCountryMappings (); 

  // Conversion processes
  void calculateCustomPoints ();
  void calculateTradePower ();
  void convertArmies ();
  void convertCores ();
  void createParties ();   
  void diplomacy ();
  void leaders (); 
  void mergePops ();
  void moveCapitals ();  
  void moveFactories ();
  void moveRgos ();
  void movePops ();
  void nationalValues ();
  void navalBases ();  
  void reassignProvinces ();
  void redistributeGoods (); 
  void techLevels ();  
  
  // Storage and maps 
  objvec eu3Provinces;
  objvec eu3Countries;
  objvec vicProvinces;
  objvec vicCountries;
  objvec vicFactories; 
  objvec buildingTypes;
  objvec techs;
  objvec regions;
  objvec goods;
  objvec continents;
  objvec historicalPrices;
  map<string, Object*> tagToPartiesMap; 
  map<string, Object*> trigMods; 
  map<string, Object*> nameToDecisionMap; 
  map<string, Object*> nameToModifierMap; 
  vector<double> productionEfficiencies; 
  map<Object*, PopSourceInfo*> eu3CountryToPopSourceMap; 
  map<string, vector<double> > supply;
  map<string, vector<double> > demand;
  map<string, vector<double> > prices; 
  map<Object*, vector<ProvinceHistory*> > eu3ProvinceToHistoryMap;
  map<Object*, vector<NationHistory*> > eu3NationToHistoryMap; 
  map<string, Object*> eu3ProvNumToEu3ProvInfoMap;
  map<string, Object*> vicProvNumToVicProvInfoMap; 
  map<string, int> eu3TagToSizeMap;
  map<string, string> eu3CultureToVicCultureMap;
  map<Object*, objvec> eu3ProvinceToVicProvincesMap;
  map<Object*, objvec> vicProvinceToEu3ProvincesMap;
  map<Object*, Object*> eu3CountryToVicCountryMap;
  map<Object*, objvec> vicCountryToEu3CountriesMap;
  map<string, Object*> eu3TagToEu3CountryMap;
  map<string, Object*> vicTagToVicCountryMap;
  map<string, Object*> vicProvIdToVicProvinceMap;
  int gameDays;  // Days at game date
  int gameStartDays; // Days at start
  vector<string> sliders;
  map<string, Object*> religionMap;
  map<string, Object*> governmentMap;
  map<string, Object*> ideasMap;
  Object* countryPointers;
  map<string, Object*> leaderMap;
  Object* traitObject;
  objvec provdirs;
  Object* vicTechs;
  Object* vicInventions; 
  map<string, double> goodsToRedistribute;
  Object* eu3ContinentObject;
  Object* rgoList; // Redistribution object, containing province switch assignments. 
}; 



#endif

