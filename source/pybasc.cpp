#ifndef STANDALONE
#include <Python.h>
#endif

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "../include/skimage.hpp"
#include "../bayesys/bayesys3.h"
#include "../include/options.hpp"

using namespace std;

skimage dirtyMap, dirtyBeam, primaryBeam;
uint32_t mapsize, mapdepth, modelIndex;
double sigma, fluxscale, freqscale;
bool cubeSwitch = false;
optDict options;

struct UserCommonStr {
  uint32_t nmodels;
  uint32_t maxmodels;
  uint32_t burnin;
  double cool;
  vector <uint16_t> natoms;
  vector <double> x;
  vector <double> y;
  vector <double> F;
  vector <double> fmu;
  vector <double> fsig;
  vector <uint32_t> modelIndex;
};

UserCommonStr UserCommon[1];
CommonStr Common;

extern "C" {
int UserBuild(double *like, CommonStr *Common, ObjectStr* Member, int natoms, int dummy) {
  vector <twov> points;
  vector <double> flux;
  vector <double> fmu;
  vector <double> fsig;
  double **Cube = Member->Cubes;
  //UserCommonStr *UC = (UserCommonStr *)Common->UserCommon;

  if (natoms==0) {
    *like = -1e6;
    return 0;
  }

  for (uint32_t i=0;i<natoms;i++) {
    double x = Cube[i][0]*double(mapsize);
    double y = Cube[i][1]*double(mapsize);
    points.push_back(twov(x,y));
    flux.push_back(fluxscale*Cube[i][2]/(1.-Cube[i][2]));
    if (cubeSwitch) {
      fmu.push_back(Cube[i][3]*freqscale);
      fsig.push_back(Cube[i][4]*freqscale);
    }
  }

  if (cubeSwitch) {
    *like = dirtyMap.deconv(dirtyBeam,primaryBeam,&points[0],&flux[0],&fmu[0],&fsig[0],flux.size());
    return 1;
  }

  *like = dirtyMap.deconv(dirtyBeam,primaryBeam,&points[0],&flux[0],flux.size());

  //*like = dirtyMap.deconv(dirtyBeam,&points[0],&flux[0],flux.size());

  return 1;
}

int UserMonitor(CommonStr *Common, ObjectStr *Members) {
  double **Cube;// = Members->Cubes;
  UserCommonStr *UC = (UserCommonStr *)Common->UserCommon;

  if (Common->cool > 1.) Common->cool = 1.;
  if (Common->cool < 1.) {
    UC->burnin += 1;
    if (Common->cool>UC->cool) {
      //cout << Common->cool << " " << UC->burnin << endl;
      UC->cool += 0.1;
    }
    return 0;
  }

  UC->natoms.push_back(Members[0].Natoms);
  if (UC->nmodels == 0) {
    cout << "Burn in complete" << endl;
  }
  UC->nmodels += 1;
  for (uint32_t k=0;k<Common->ENSEMBLE;k++) {
    Cube = Members[k].Cubes;
    for (uint32_t i=0;i<Members[k].Natoms;i++) {
      UC->x.push_back(Cube[i][0]*double(mapsize));
      UC->y.push_back(Cube[i][1]*double(mapsize));
      UC->F.push_back(fluxscale*(Cube[i][2]/(1.-Cube[i][2])));
      if (cubeSwitch) {
        UC->fmu.push_back(Cube[i][3]*freqscale);
        UC->fsig.push_back(Cube[i][4]*freqscale);
      }
      UC->modelIndex.push_back(modelIndex);
    }
    modelIndex++;
  }

  if (UC->nmodels>UC->maxmodels) {
    return 1;
  } else {
    return 0;
  }
}
}

#ifndef STANDALONE
void parsefrompy(skimage *target, PyObject* source, uint32_t x, uint32_t y) {
  PyObject *datum;
  uint32_t counter;
  double value;

  target->init(x,y,1);

  if (PyList_Size(source)<(x*y)) {
    cout << "Not enough data provided" << endl;
    return;
  }

  for (uint32_t v=0;v<y;v++) {
    for (uint32_t u=0;u<x;u++) {
      counter = u+v*x;
      datum = PyList_GetItem(source, counter);
      PyArg_Parse(datum, "d", &value);
      target->set(u,v,0,value);
    }
  }
}


static PyObject* bascmodule_setgrid(PyObject *self, PyObject *args) {
  uint32_t crpix1, crpix2, mapid;
  double crval1, crval2, cdelt1, cdelt2;

  if (!PyArg_ParseTuple(args,"iiddidd", &mapid, &crpix1, &crval1, &cdelt1, &crpix2, &crval2, &cdelt2)) { return NULL; }

  if (mapid==0) {
    dirtyMap.setgrid(0,crpix1,crval1,cdelt1);
    dirtyMap.setgrid(1,crpix2,crval2,cdelt2);
  }

  if (mapid==1) {
    dirtyBeam.setgrid(0,crpix1,crval1,cdelt1);
    dirtyBeam.setgrid(1,crpix2,crval2,cdelt2);
  }

  if (mapid==2) {
    primaryBeam.setgrid(0,crpix1,crval1,cdelt1);
    primaryBeam.setgrid(1,crpix2,crval2,cdelt2);
  }

  return PyLong_FromLong(1);
}

static PyObject* bascmodule_map(PyObject *self, PyObject *args) {
  uint32_t xb,yb,id;
  PyObject *map;

  if (!PyArg_ParseTuple(args,"O!iii", &PyList_Type, &map, &xb, &yb, &id)) { return NULL; }

  if (id==0) {
    parsefrompy(&dirtyMap,map,xb,yb);
    dirtyMap.crop(xb/2, yb/2);
  }

  if (id==1) {
    parsefrompy(&dirtyBeam,map,xb,yb);
  }

  if (id==2) {
    parsefrompy(&primaryBeam,map,xb,yb);
    primaryBeam.crop(xb/2, yb/2);
    primaryBeam.unnan();
  }

  //TODO: add support for non square maps
  mapsize = xb/2;

  //TODO: add support for Cubes
  mapdepth = 1;

  return PyLong_FromLong(1);
}

static PyObject* bascmodule_getmap(PyObject *self, PyObject *args) {
  uint32_t mapIndex, listSize, axisSize;
  PyObject *returnList;
  skimage *mapPtr;

  if (!PyArg_ParseTuple(args,"i", &mapIndex)) { return NULL; }

  if (mapIndex==1) {
    axisSize = mapsize*2;
    mapPtr = &dirtyBeam;
  } else {
    axisSize = mapsize;
    if (mapIndex==0) { mapPtr = &dirtyMap; }
    if (mapIndex==2) { mapPtr = &primaryBeam; }
  }

  listSize = axisSize*axisSize;
  returnList = PyList_New(listSize);
  for (uint32_t y=0;y<axisSize;y++) {
      for (uint32_t x=0;x<axisSize;x++) {
        PyList_SetItem(returnList, x+y*axisSize, PyFloat_FromDouble((*mapPtr).get(x,y,0)));
      }
  }

  return returnList;
}

static PyObject* bascmodule_init(PyObject *self, PyObject *args) {
  Common.UserCommon = (void *)UserCommon;

  return PyLong_FromLong(1);
}

static PyObject* bascmodule_setOption(PyObject *self, PyObject *args) {
  char *key;
  char *value;

  if (!PyArg_ParseTuple(args,"ss", &key, &value)) { return NULL; }

  if (strcmp(key,"MinAtoms")==0) { Common.MinAtoms = atoi(value); }
  if (strcmp(key,"MaxAtoms")==0) { Common.MaxAtoms = atoi(value); }
  if (strcmp(key,"Alpha")==0) { Common.Alpha = atoi(value); }
  if (strcmp(key,"Valency")==0) { Common.Valency = atoi(value); }
  if (strcmp(key,"Iseed")==0) { Common.Iseed = atoi(value); }
  if (strcmp(key,"Ensemble")==0) { Common.ENSEMBLE = atoi(value); }
  if (strcmp(key,"Method")==0) { Common.Method = atoi(value); }
  if (strcmp(key,"Rate")==0) { Common.Rate = atoi(value); }
  if (strcmp(key,"maxmodels")==0) { UserCommon->maxmodels = atoi(value); }

  return PyLong_FromLong(1);
}


static PyObject* bascmodule_run(PyObject *self, PyObject *args) {
  uint32_t code;
  ObjectStr *Members;
  fstream chainfile;

  //dirtyMap.init(512,512,1);
  //dirtyBeam.init(1024,1024,1);
  //primaryBeam.init(512,512,1);

  fluxscale = dirtyMap.noise(primaryBeam);
  dirtyMap.setnoise(fluxscale);

  if (mapdepth>1) {
    Common.Ndim = 5;
  } else {
    Common.Ndim = 3;
  }

  /*options.readFile("config.txt");

  Common.MinAtoms = options.getint("MinAtoms");
  Common.MaxAtoms = options.getint("MaxAtoms");
  Common.Alpha = options.getint("Alpha");
  Common.Valency = options.getint("Valency");
  Common.Iseed =  options.getint("Iseed");
  Common.ENSEMBLE =  options.getint("Ensemble");
  Common.Method =  options.getint("Method");
  Common.Rate =  options.getint("Rate");
  Common.UserCommon = (void *)UserCommon;
  UserCommon->maxmodels =  options.getint("maxmodels");*/
  UserCommon->nmodels = 0;
  UserCommon->burnin = 0;
  UserCommon->cool = 0;

  Members = new ObjectStr[Common.ENSEMBLE];
  code = BayeSys3(&Common,Members);

  chainfile.open("chain.txt", fstream::out);
  for (auto i=0;i<UserCommon->x.size();i++) {
    chainfile << fixed << setprecision(9) << UserCommon->x[i] << " " << UserCommon->y[i] << " " << UserCommon->F[i] << " " << UserCommon->modelIndex[i];
    /*if (cubeSwitch) {
      chainfile << " " << UserCommon->fmu[i] << " " << UserCommon->fsig[i];
    }*/
    chainfile << endl;
  }
  chainfile.close();

  return PyLong_FromLong(code);
}

static PyObject* bascmodule_chain(PyObject *self, PyObject *args) {
  uint32_t colIndex;
  PyObject *returnList = PyList_New(UserCommon->nmodels);
  vector <double> *colPtr;

  if (!PyArg_ParseTuple(args,"i", &colIndex)) { return NULL; }

  if (colIndex==0) { colPtr=&UserCommon->x; }
  if (colIndex==1) { colPtr=&UserCommon->y; }
  if (colIndex==2) { colPtr=&UserCommon->F; }
  if (colIndex==3) {
    vector <uint32_t> *intPtr=&UserCommon->modelIndex;
    for (uint32_t i=0;i<UserCommon->nmodels;i++) {
      PyList_SetItem(returnList, i, PyInt_FromLong((*intPtr)[i]));
    }
    return returnList;
  }

  for (uint32_t i=0;i<UserCommon->nmodels;i++) {
    PyList_SetItem(returnList, i, PyFloat_FromDouble((*colPtr)[i]));
  }

  return returnList;
}

static PyObject* bascmodule_show(PyObject *self, PyObject *args) {
  uint32_t item;
  skimage *skPtr;

  if (!PyArg_ParseTuple(args,"i", &item)) { return NULL; }

  if (item==0) { skPtr = &dirtyMap; }
  if (item==1) { skPtr = &dirtyBeam; }
  if (item==2) { skPtr = &primaryBeam; }

  (*skPtr).scan(32, (*skPtr).mean(), 0.5*(*skPtr).stdev((*skPtr).mean()));
  return PyLong_FromLong(1);
}

static PyObject* bascmodule_version(PyObject *self, PyObject *args) {
  cout << "Version number 0.1" << endl;
  return PyLong_FromLong(1);
}

static PyMethodDef bascmethods[] = {
  {"version", bascmodule_version, METH_VARARGS, ""},
  {"grid", bascmodule_setgrid, METH_VARARGS, ""},
  {"map", bascmodule_map, METH_VARARGS, ""},
  {"getmap", bascmodule_getmap, METH_VARARGS, ""},
  {"run", bascmodule_run, METH_VARARGS, ""},
  {"chain", bascmodule_chain, METH_VARARGS, ""},
  {"show", bascmodule_show, METH_VARARGS, ""},
  {"init", bascmodule_init, METH_VARARGS, ""},
  {"option", bascmodule_setOption, METH_VARARGS, ""},
  {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION > 2
static struct PyModuleDef bascmodule = {
  PyModuleDef_HEAD_INIT,
  "bascmod",
  NULL,
  -1,
  bascmethods
};
PyMODINIT_FUNC
PyInit_bascmod(void) {
  return PyModule_Create(&bascmodule);
#else
PyMODINIT_FUNC
initbascmod(void) {
  (void) Py_InitModule("bascmod", bascmethods);
#endif
}
#else
int main() {
  CommonStr Common;
  ObjectStr *Members;

  dirtyMap.init(10,10,1);
  dirtyBeam.init(20,20,1);
  primaryBeam.init(10,10,1);

  options.readFile("config.txt");

  Common.Ndim = 3;
  Common.MinAtoms = options.getint("MinAtoms");
  Common.MaxAtoms = options.getint("MaxAtoms");
  Common.Alpha = options.getint("Alpha");
  Common.Valency = options.getint("Valency");
  Common.Iseed =  options.getint("Iseed");
  Common.ENSEMBLE =  options.getint("Ensemble");
  Common.Method =  options.getint("Method");
  Common.Rate =  options.getint("Rate");
  Common.UserCommon = (void *)UserCommon;
  UserCommon->nmodels = 0;
  UserCommon->maxmodels =  options.getint("maxmodels");
  UserCommon->burnin = 0;
  UserCommon->cool = 0;

  Members = new ObjectStr[Common.ENSEMBLE];

  cout << BayeSys3(&Common,Members) << endl;
}
#endif
