// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "PyScriptingBackend.h"

#include <Python.h>
#include <string>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "DiscIO/Filesystem.h"

#include "Scripting/Python/coroutine.h"
#include "Scripting/Python/Modules/controllermodule.h"
#include "Scripting/Python/Modules/doliomodule.h"
#include "Scripting/Python/Modules/dolphinmodule.h"
#include "Scripting/Python/Modules/eventmodule.h"
#include "Scripting/Python/Modules/guimodule.h"
#include "Scripting/Python/Modules/memorymodule.h"
#include "Scripting/Python/Modules/savestatemodule.h"

namespace PyScripting
{

static PyThreadState* InitMainPythonInterpreter()
{
#ifdef _WIN32
  static const std::wstring python_home = UTF8ToWString(File::GetExeDirectory()) + L"/python-embed";
  static const std::wstring python_path =
      UTF8ToWString(File::GetCurrentDir()) + L";" +
      UTF8ToWString(File::GetExeDirectory()) + L"/python-embed/python38.zip;" +
      UTF8ToWString(File::GetExeDirectory()) + L"/python-embed;" +
      UTF8ToWString(File::GetExeDirectory());
#endif

  if (PyImport_AppendInittab("dolio_stdout", PyInit_dolio_stdout) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolio_stdout to builtins");
  if (PyImport_AppendInittab("dolio_stderr", PyInit_dolio_stderr) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolio_stderr to builtins");
  if (PyImport_AppendInittab("dolphin_memory", PyInit_memory) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin_memory to builtins");
  if (PyImport_AppendInittab("dolphin_event", PyInit_event) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin_event to builtins");
  if (PyImport_AppendInittab("dolphin_gui", PyInit_gui) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin_gui to builtins");
  if (PyImport_AppendInittab("dolphin_savestate", PyInit_savestate) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin_savestate to builtins");
  if (PyImport_AppendInittab("dolphin_controller", PyInit_controller) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin_controller to builtins");

  if (PyImport_AppendInittab("dolphin", PyInit_dolphin) == -1)
    ERROR_LOG(SCRIPTING, "failed to add dolphin to builtins");

#ifdef _WIN32
  Py_SetPythonHome(const_cast<wchar_t*>(python_home.c_str()));
  Py_SetPath(python_path.c_str());
#endif
  INFO_LOG(SCRIPTING, "Initializing embedded python... %s", Py_GetVersion());
  Py_InitializeEx(0);

  // Starting with Python 3.7 Py_Initialize* also initializes the GIL in a locked state.
  // This might be the same issue: https://bugs.python.org/issue38680
  // We need to manually unlock the GIL here to start off in an unlocked state,
  // because we handle locking and unlocking the GIL with a RAII-wrapper.
  return PyEval_SaveThread();
}

static void Init(std::filesystem::path script_filepath)
{
  if (script_filepath.is_relative())
    script_filepath = File::GetCurrentDir() / script_filepath;
  std::string script_filepath_str = script_filepath.string();

  if (!std::filesystem::exists(script_filepath))
  {
    ERROR_LOG(SCRIPTING, "Script filepath was not found: %s", script_filepath_str.c_str());
    return;
  }

  PyCompilerFlags flags = {PyCF_ALLOW_TOP_LEVEL_AWAIT};
  PyObject* globals = PyModule_GetDict(PyImport_AddModule("__main__"));
  PyObject* execution_result =
      PyRun_FileExFlags(fopen(script_filepath_str.c_str(), "rb"), script_filepath_str.c_str(),
                        Py_file_input, globals, globals, true, &flags);

  if (execution_result == nullptr)
  {
    PyErr_Print();
    return;
  }

  if (PyCoro_CheckExact(execution_result))
  {
    Py::Object event_module = Py::Wrap(PyImport_ImportModule("dolphin_event"));
    HandleNewCoroutine(event_module, Py::Wrap(execution_result));
  }
}

static void ShutdownMainPythonInterpreter()
{
  if (Py_FinalizeEx() != 0)
  {
    ERROR_LOG(SCRIPTING, "Unexpectedly failed to finalize python");
  }
}

PyScriptingBackend::PyScriptingBackend(std::filesystem::path script_filepath,
                                       API::EventHub& event_hub, API::Gui& gui,
                                       API::GCManip& gc_manip, API::WiiManip& wii_manip)
    : m_event_hub(event_hub), m_gui(gui), m_gc_manip(gc_manip), m_wii_manip(wii_manip)
{
  std::lock_guard lock{s_bookkeeping_lock};
  if (s_instances.empty())
  {
    s_main_threadstate = InitMainPythonInterpreter();
  }
  PyEval_RestoreThread(s_main_threadstate);
  m_interp_threadstate = Py_NewInterpreter();
  PyThreadState_Swap(m_interp_threadstate);
  u64 interp_id = PyInterpreterState_GetID(m_interp_threadstate->interp);
  s_instances[interp_id] = this;

  {
    // new scope because we need to drop these PyObjects before we release the GIL
    // below (PyEval_SaveThread) because DECREF-ing them needs the GIL to be held.
    Py::Object result_stdout = Py::Wrap(PyImport_ImportModule("dolio_stdout"));
    if (result_stdout.IsNull())
    {
      ERROR_LOG(SCRIPTING, "Error auto-importing dolio_stdout for stdout");
      PyErr_Print();
    }
    Py::Object result_stderr = Py::Wrap(PyImport_ImportModule("dolio_stderr"));
    if (result_stderr.IsNull())
    {
      ERROR_LOG(SCRIPTING, "Error auto-importing dolio_stderr for stderr");
      PyErr_Print();
    }
  }

  Init(script_filepath);

  PyEval_SaveThread();
}

PyScriptingBackend::~PyScriptingBackend()
{
  std::lock_guard lock{s_bookkeeping_lock};
  if (m_interp_threadstate == nullptr)
    return;  // we've been moved from (if moving was implemented)
  PyEval_RestoreThread(m_interp_threadstate);
  u64 interp_id = PyInterpreterState_GetID(m_interp_threadstate->interp);
  s_instances.erase(interp_id);
  Py_EndInterpreter(m_interp_threadstate);
  PyThreadState_Swap(s_main_threadstate);
  if (s_instances.empty())
  {
    ShutdownMainPythonInterpreter();
    s_main_threadstate = nullptr;
  }
  else
  {
    PyEval_SaveThread();
  }
}

// Each PyScriptingBackend manages one python sub-interpreter.
// But python's C api is stateful instead of object oriented,
// so we need this lookup to bridge the gap.
PyScriptingBackend* PyScriptingBackend::GetCurrent()
{
  PyInterpreterState* interp_state = PyThreadState_Get()->interp;
  u64 interp_id = PyInterpreterState_GetID(interp_state);
  return s_instances[interp_id];
}

API::EventHub* PyScriptingBackend::GetEventHub()
{
  return &m_event_hub;
}

API::Gui* PyScriptingBackend::GetGui()
{
  return &m_gui;
}

API::GCManip* PyScriptingBackend::GetGCManip()
{
  return &m_gc_manip;
}

API::WiiManip* PyScriptingBackend::GetWiiManip()
{
  return &m_wii_manip;
}

std::map<u64, PyScriptingBackend*> PyScriptingBackend::s_instances;
PyThreadState* PyScriptingBackend::s_main_threadstate;
std::mutex PyScriptingBackend::s_bookkeeping_lock;

}  // namespace PyScripting
