#include "duckdb_python/pyconnection/pyconnection.hpp"

#include "duckdb/catalog/default/default_types.hpp"
#include "duckdb/common/arrow/arrow.hpp"
#include "duckdb/common/enums/file_compression_type.hpp"
#include "duckdb/common/printer.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/table/read_csv.hpp"
#include "duckdb/main/client_config.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/db_instance_cache.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/main/relation/read_csv_relation.hpp"
#include "duckdb/main/relation/read_json_relation.hpp"
#include "duckdb/main/relation/value_relation.hpp"
#include "duckdb/main/relation/view_relation.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb_python/arrow/arrow_array_stream.hpp"
#include "duckdb_python/map.hpp"
#include "duckdb_python/pandas/pandas_scan.hpp"
#include "duckdb_python/pyrelation.hpp"
#include "duckdb_python/pystatement.hpp"
#include "duckdb_python/pyresult.hpp"
#include "duckdb_python/python_conversion.hpp"
#include "duckdb_python/numpy/numpy_type.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb_python/jupyter_progress_bar_display.hpp"
#include "duckdb_python/pyfilesystem.hpp"
#include "duckdb/main/client_config.hpp"
#include "duckdb/function/table/read_csv.hpp"
#include "duckdb/common/enums/file_compression_type.hpp"
#include "duckdb/catalog/default/default_types.hpp"
#include "duckdb/main/relation/value_relation.hpp"
#include "duckdb_python/filesystem_object.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb_python/pandas/pandas_scan.hpp"
#include "duckdb_python/python_objects.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb_python/pybind11/conversions/exception_handling_enum.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"
#include "duckdb/catalog/catalog_entry/scalar_function_catalog_entry.hpp"
#include "duckdb/main/pending_query_result.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb_python/python_replacement_scan.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/main/materialized_query_result.hpp"
#include "duckdb/main/stream_query_result.hpp"
#include "duckdb/main/relation/materialized_relation.hpp"
#include "duckdb/main/relation/query_relation.hpp"
#include "duckdb/parser/statement/load_statement.hpp"
#include "duckdb_python/expression/pyexpression.hpp"

#include <random>

#include "duckdb/common/printer.hpp"

namespace duckdb {

DefaultConnectionHolder DuckDBPyConnection::default_connection;                        // NOLINT: allow global
DBInstanceCache instance_cache;                                                        // NOLINT: allow global
shared_ptr<PythonImportCache> DuckDBPyConnection::import_cache = nullptr;              // NOLINT: allow global
PythonEnvironmentType DuckDBPyConnection::environment = PythonEnvironmentType::NORMAL; // NOLINT: allow global
std::string DuckDBPyConnection::formatted_python_version = "";

DuckDBPyConnection::~DuckDBPyConnection() {
	try {
		py::gil_scoped_release gil;
		// Release any structures that do not need to hold the GIL here
		con.SetDatabase(nullptr);
		con.SetConnection(nullptr);
	} catch (...) { // NOLINT
	}
}

void DuckDBPyConnection::DetectEnvironment() {
	// Get the formatted Python version
	py::module_ sys = py::module_::import("sys");
	py::object version_info = sys.attr("version_info");
	int major = py::cast<int>(version_info.attr("major"));
	int minor = py::cast<int>(version_info.attr("minor"));
	DuckDBPyConnection::formatted_python_version = std::to_string(major) + "." + std::to_string(minor);

	// If __main__ does not have a __file__ attribute, we are in interactive mode
	auto main_module = py::module_::import("__main__");
	if (py::hasattr(main_module, "__file__")) {
		return;
	}
	DuckDBPyConnection::environment = PythonEnvironmentType::INTERACTIVE;
	if (!ModuleIsLoaded<IpythonCacheItem>()) {
		return;
	}

	// Check to see if we are in a Jupyter Notebook
	auto &import_cache_py = *DuckDBPyConnection::ImportCache();
	auto get_ipython = import_cache_py.IPython.get_ipython();
	if (get_ipython.ptr() == nullptr) {
		// Could either not load the IPython module, or it has no 'get_ipython' attribute
		return;
	}
	auto ipython = get_ipython();
	if (!py::hasattr(ipython, "config")) {
		return;
	}
	py::dict ipython_config = ipython.attr("config");
	if (ipython_config.contains("IPKernelApp")) {
		DuckDBPyConnection::environment = PythonEnvironmentType::JUPYTER;
	}
	return;
}

bool DuckDBPyConnection::DetectAndGetEnvironment() {
	DuckDBPyConnection::DetectEnvironment();
	return DuckDBPyConnection::IsInteractive();
}

bool DuckDBPyConnection::IsJupyter() {
	return DuckDBPyConnection::environment == PythonEnvironmentType::JUPYTER;
}

std::string DuckDBPyConnection::FormattedPythonVersion() {
	return DuckDBPyConnection::formatted_python_version;
}

// NOTE: this function is generated by tools/pythonpkg/scripts/generate_connection_methods.py.
// Do not edit this function manually, your changes will be overwritten!

static void InitializeConnectionMethods(py::class_<DuckDBPyConnection, shared_ptr<DuckDBPyConnection>> &m) {
	m.def("cursor", &DuckDBPyConnection::Cursor, "Create a duplicate of the current connection");
	m.def("register_filesystem", &DuckDBPyConnection::RegisterFilesystem, "Register a fsspec compliant filesystem",
	      py::arg("filesystem"));
	m.def("unregister_filesystem", &DuckDBPyConnection::UnregisterFilesystem, "Unregister a filesystem",
	      py::arg("name"));
	m.def("list_filesystems", &DuckDBPyConnection::ListFilesystems,
	      "List registered filesystems, including builtin ones");
	m.def("filesystem_is_registered", &DuckDBPyConnection::FileSystemIsRegistered,
	      "Check if a filesystem with the provided name is currently registered", py::arg("name"));
	m.def("create_function", &DuckDBPyConnection::RegisterScalarUDF,
	      "Create a DuckDB function out of the passing in Python function so it can be used in queries",
	      py::arg("name"), py::arg("function"), py::arg("parameters") = py::none(), py::arg("return_type") = py::none(),
	      py::kw_only(), py::arg("type") = PythonUDFType::NATIVE,
	      py::arg("null_handling") = FunctionNullHandling::DEFAULT_NULL_HANDLING,
	      py::arg("exception_handling") = PythonExceptionHandling::FORWARD_ERROR, py::arg("side_effects") = false);
	m.def("remove_function", &DuckDBPyConnection::UnregisterUDF, "Remove a previously created function",
	      py::arg("name"));
	m.def("sqltype", &DuckDBPyConnection::Type, "Create a type object by parsing the 'type_str' string",
	      py::arg("type_str"));
	m.def("dtype", &DuckDBPyConnection::Type, "Create a type object by parsing the 'type_str' string",
	      py::arg("type_str"));
	m.def("type", &DuckDBPyConnection::Type, "Create a type object by parsing the 'type_str' string",
	      py::arg("type_str"));
	m.def("array_type", &DuckDBPyConnection::ArrayType, "Create an array type object of 'type'",
	      py::arg("type").none(false), py::arg("size"));
	m.def("list_type", &DuckDBPyConnection::ListType, "Create a list type object of 'type'",
	      py::arg("type").none(false));
	m.def("union_type", &DuckDBPyConnection::UnionType, "Create a union type object from 'members'",
	      py::arg("members").none(false));
	m.def("string_type", &DuckDBPyConnection::StringType, "Create a string type with an optional collation",
	      py::arg("collation") = "");
	m.def("enum_type", &DuckDBPyConnection::EnumType,
	      "Create an enum type of underlying 'type', consisting of the list of 'values'", py::arg("name"),
	      py::arg("type"), py::arg("values"));
	m.def("decimal_type", &DuckDBPyConnection::DecimalType, "Create a decimal type with 'width' and 'scale'",
	      py::arg("width"), py::arg("scale"));
	m.def("struct_type", &DuckDBPyConnection::StructType, "Create a struct type object from 'fields'",
	      py::arg("fields"));
	m.def("row_type", &DuckDBPyConnection::StructType, "Create a struct type object from 'fields'", py::arg("fields"));
	m.def("map_type", &DuckDBPyConnection::MapType, "Create a map type object from 'key_type' and 'value_type'",
	      py::arg("key").none(false), py::arg("value").none(false));
	m.def("duplicate", &DuckDBPyConnection::Cursor, "Create a duplicate of the current connection");
	m.def("execute", &DuckDBPyConnection::Execute,
	      "Execute the given SQL query, optionally using prepared statements with parameters set", py::arg("query"),
	      py::arg("parameters") = py::none());
	m.def("executemany", &DuckDBPyConnection::ExecuteMany,
	      "Execute the given prepared statement multiple times using the list of parameter sets in parameters",
	      py::arg("query"), py::arg("parameters") = py::none());
	m.def("close", &DuckDBPyConnection::Close, "Close the connection");
	m.def("interrupt", &DuckDBPyConnection::Interrupt, "Interrupt pending operations");
	m.def("query_progress", &DuckDBPyConnection::QueryProgress, "Query progress of pending operation");
	m.def("fetchone", &DuckDBPyConnection::FetchOne, "Fetch a single row from a result following execute");
	m.def("fetchmany", &DuckDBPyConnection::FetchMany, "Fetch the next set of rows from a result following execute",
	      py::arg("size") = 1);
	m.def("fetchall", &DuckDBPyConnection::FetchAll, "Fetch all rows from a result following execute");
	m.def("fetchnumpy", &DuckDBPyConnection::FetchNumpy, "Fetch a result as list of NumPy arrays following execute");
	m.def("fetchdf", &DuckDBPyConnection::FetchDF, "Fetch a result as DataFrame following execute()", py::kw_only(),
	      py::arg("date_as_object") = false);
	m.def("fetch_df", &DuckDBPyConnection::FetchDF, "Fetch a result as DataFrame following execute()", py::kw_only(),
	      py::arg("date_as_object") = false);
	m.def("df", &DuckDBPyConnection::FetchDF, "Fetch a result as DataFrame following execute()", py::kw_only(),
	      py::arg("date_as_object") = false);
	m.def("fetch_df_chunk", &DuckDBPyConnection::FetchDFChunk,
	      "Fetch a chunk of the result as DataFrame following execute()", py::arg("vectors_per_chunk") = 1,
	      py::kw_only(), py::arg("date_as_object") = false);
	m.def("pl", &DuckDBPyConnection::FetchPolars, "Fetch a result as Polars DataFrame following execute()",
	      py::arg("rows_per_batch") = 1000000, py::kw_only(), py::arg("lazy") = false);
	m.def("fetch_arrow_table", &DuckDBPyConnection::FetchArrow, "Fetch a result as Arrow table following execute()",
	      py::arg("rows_per_batch") = 1000000);
	m.def("arrow", &DuckDBPyConnection::FetchArrow, "Fetch a result as Arrow table following execute()",
	      py::arg("rows_per_batch") = 1000000);
	m.def("fetch_record_batch", &DuckDBPyConnection::FetchRecordBatchReader,
	      "Fetch an Arrow RecordBatchReader following execute()", py::arg("rows_per_batch") = 1000000);
	m.def("torch", &DuckDBPyConnection::FetchPyTorch, "Fetch a result as dict of PyTorch Tensors following execute()");
	m.def("tf", &DuckDBPyConnection::FetchTF, "Fetch a result as dict of TensorFlow Tensors following execute()");
	m.def("begin", &DuckDBPyConnection::Begin, "Start a new transaction");
	m.def("commit", &DuckDBPyConnection::Commit, "Commit changes performed within a transaction");
	m.def("rollback", &DuckDBPyConnection::Rollback, "Roll back changes performed within a transaction");
	m.def("checkpoint", &DuckDBPyConnection::Checkpoint,
	      "Synchronizes data in the write-ahead log (WAL) to the database data file (no-op for in-memory connections)");
	m.def("append", &DuckDBPyConnection::Append, "Append the passed DataFrame to the named table",
	      py::arg("table_name"), py::arg("df"), py::kw_only(), py::arg("by_name") = false);
	m.def("register", &DuckDBPyConnection::RegisterPythonObject,
	      "Register the passed Python Object value for querying with a view", py::arg("view_name"),
	      py::arg("python_object"));
	m.def("unregister", &DuckDBPyConnection::UnregisterPythonObject, "Unregister the view name", py::arg("view_name"));
	m.def("table", &DuckDBPyConnection::Table, "Create a relation object for the named table", py::arg("table_name"));
	m.def("view", &DuckDBPyConnection::View, "Create a relation object for the named view", py::arg("view_name"));
	m.def("values", &DuckDBPyConnection::Values, "Create a relation object from the passed values");
	m.def("table_function", &DuckDBPyConnection::TableFunction,
	      "Create a relation object from the named table function with given parameters", py::arg("name"),
	      py::arg("parameters") = py::none());
	m.def("read_json", &DuckDBPyConnection::ReadJSON, "Create a relation object from the JSON file in 'name'",
	      py::arg("path_or_buffer"), py::kw_only(), py::arg("columns") = py::none(),
	      py::arg("sample_size") = py::none(), py::arg("maximum_depth") = py::none(), py::arg("records") = py::none(),
	      py::arg("format") = py::none(), py::arg("date_format") = py::none(), py::arg("timestamp_format") = py::none(),
	      py::arg("compression") = py::none(), py::arg("maximum_object_size") = py::none(),
	      py::arg("ignore_errors") = py::none(), py::arg("convert_strings_to_integers") = py::none(),
	      py::arg("field_appearance_threshold") = py::none(), py::arg("map_inference_threshold") = py::none(),
	      py::arg("maximum_sample_files") = py::none(), py::arg("filename") = py::none(),
	      py::arg("hive_partitioning") = py::none(), py::arg("union_by_name") = py::none(),
	      py::arg("hive_types") = py::none(), py::arg("hive_types_autocast") = py::none());
	m.def("extract_statements", &DuckDBPyConnection::ExtractStatements,
	      "Parse the query string and extract the Statement object(s) produced", py::arg("query"));
	m.def("sql", &DuckDBPyConnection::RunQuery,
	      "Run a SQL query. If it is a SELECT statement, create a relation object from the given SQL query, otherwise "
	      "run the query as-is.",
	      py::arg("query"), py::kw_only(), py::arg("alias") = "", py::arg("params") = py::none());
	m.def("query", &DuckDBPyConnection::RunQuery,
	      "Run a SQL query. If it is a SELECT statement, create a relation object from the given SQL query, otherwise "
	      "run the query as-is.",
	      py::arg("query"), py::kw_only(), py::arg("alias") = "", py::arg("params") = py::none());
	m.def("from_query", &DuckDBPyConnection::RunQuery,
	      "Run a SQL query. If it is a SELECT statement, create a relation object from the given SQL query, otherwise "
	      "run the query as-is.",
	      py::arg("query"), py::kw_only(), py::arg("alias") = "", py::arg("params") = py::none());
	m.def("read_csv", &DuckDBPyConnection::ReadCSV, "Create a relation object from the CSV file in 'name'",
	      py::arg("path_or_buffer"), py::kw_only());
	m.def("from_csv_auto", &DuckDBPyConnection::ReadCSV, "Create a relation object from the CSV file in 'name'",
	      py::arg("path_or_buffer"), py::kw_only());
	m.def("from_df", &DuckDBPyConnection::FromDF, "Create a relation object from the DataFrame in df", py::arg("df"));
	m.def("from_arrow", &DuckDBPyConnection::FromArrow, "Create a relation object from an Arrow object",
	      py::arg("arrow_object"));
	m.def("from_parquet", &DuckDBPyConnection::FromParquet,
	      "Create a relation object from the Parquet files in file_glob", py::arg("file_glob"),
	      py::arg("binary_as_string") = false, py::kw_only(), py::arg("file_row_number") = false,
	      py::arg("filename") = false, py::arg("hive_partitioning") = false, py::arg("union_by_name") = false,
	      py::arg("compression") = py::none());
	m.def("read_parquet", &DuckDBPyConnection::FromParquet,
	      "Create a relation object from the Parquet files in file_glob", py::arg("file_glob"),
	      py::arg("binary_as_string") = false, py::kw_only(), py::arg("file_row_number") = false,
	      py::arg("filename") = false, py::arg("hive_partitioning") = false, py::arg("union_by_name") = false,
	      py::arg("compression") = py::none());
	m.def("from_parquet", &DuckDBPyConnection::FromParquets,
	      "Create a relation object from the Parquet files in file_globs", py::arg("file_globs"),
	      py::arg("binary_as_string") = false, py::kw_only(), py::arg("file_row_number") = false,
	      py::arg("filename") = false, py::arg("hive_partitioning") = false, py::arg("union_by_name") = false,
	      py::arg("compression") = py::none());
	m.def("read_parquet", &DuckDBPyConnection::FromParquets,
	      "Create a relation object from the Parquet files in file_globs", py::arg("file_globs"),
	      py::arg("binary_as_string") = false, py::kw_only(), py::arg("file_row_number") = false,
	      py::arg("filename") = false, py::arg("hive_partitioning") = false, py::arg("union_by_name") = false,
	      py::arg("compression") = py::none());
	m.def("get_table_names", &DuckDBPyConnection::GetTableNames, "Extract the required table names from a query",
	      py::arg("query"), py::kw_only(), py::arg("qualified") = false);
	m.def("install_extension", &DuckDBPyConnection::InstallExtension,
	      "Install an extension by name, with an optional version and/or repository to get the extension from",
	      py::arg("extension"), py::kw_only(), py::arg("force_install") = false, py::arg("repository") = py::none(),
	      py::arg("repository_url") = py::none(), py::arg("version") = py::none());
	m.def("load_extension", &DuckDBPyConnection::LoadExtension, "Load an installed extension", py::arg("extension"));
} // END_OF_CONNECTION_METHODS

void DuckDBPyConnection::UnregisterFilesystem(const py::str &name) {
	auto &database = con.GetDatabase();
	auto &fs = database.GetFileSystem();

	fs.UnregisterSubSystem(name);
}

void DuckDBPyConnection::RegisterFilesystem(AbstractFileSystem filesystem) {
	PythonGILWrapper gil_wrapper;

	auto &database = con.GetDatabase();
	if (!py::isinstance<AbstractFileSystem>(filesystem)) {
		throw InvalidInputException("Bad filesystem instance");
	}

	auto &fs = database.GetFileSystem();

	auto protocol = filesystem.attr("protocol");
	if (protocol.is_none() || py::str("abstract").equal(protocol)) {
		throw InvalidInputException("Must provide concrete fsspec implementation");
	}

	vector<string> protocols;
	if (py::isinstance<py::str>(protocol)) {
		protocols.push_back(py::str(protocol));
	} else {
		for (const auto &sub_protocol : protocol) {
			protocols.push_back(py::str(sub_protocol));
		}
	}

	fs.RegisterSubSystem(make_uniq<PythonFilesystem>(std::move(protocols), std::move(filesystem)));
}

py::list DuckDBPyConnection::ListFilesystems() {
	auto &database = con.GetDatabase();
	auto subsystems = database.GetFileSystem().ListSubSystems();
	py::list names;
	for (auto &name : subsystems) {
		names.append(py::str(name));
	}
	return names;
}

py::list DuckDBPyConnection::ExtractStatements(const string &query) {
	py::list result;
	auto &connection = con.GetConnection();
	auto statements = connection.ExtractStatements(query);
	for (auto &statement : statements) {
		result.append(make_uniq<DuckDBPyStatement>(std::move(statement)));
	}
	return result;
}

bool DuckDBPyConnection::FileSystemIsRegistered(const string &name) {
	auto &database = con.GetDatabase();
	auto subsystems = database.GetFileSystem().ListSubSystems();
	return std::find(subsystems.begin(), subsystems.end(), name) != subsystems.end();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::UnregisterUDF(const string &name) {
	auto entry = registered_functions.find(name);
	if (entry == registered_functions.end()) {
		// Not registered or already unregistered
		throw InvalidInputException("No function by the name of '%s' was found in the list of registered functions",
		                            name);
	}

	auto &connection = con.GetConnection();
	auto &context = *connection.context;

	context.RunFunctionInTransaction([&]() {
		// create function
		auto &catalog = Catalog::GetCatalog(context, SYSTEM_CATALOG);
		DropInfo info;
		info.type = CatalogType::SCALAR_FUNCTION_ENTRY;
		info.name = name;
		info.allow_drop_internal = true;
		info.cascade = false;
		info.if_not_found = OnEntryNotFound::THROW_EXCEPTION;
		catalog.DropEntry(context, info);
	});
	registered_functions.erase(entry);

	return shared_from_this();
}

shared_ptr<DuckDBPyConnection>
DuckDBPyConnection::RegisterScalarUDF(const string &name, const py::function &udf, const py::object &parameters_p,
                                      const shared_ptr<DuckDBPyType> &return_type_p, PythonUDFType type,
                                      FunctionNullHandling null_handling, PythonExceptionHandling exception_handling,
                                      bool side_effects) {
	auto &connection = con.GetConnection();
	auto &context = *connection.context;

	if (context.transaction.HasActiveTransaction()) {
		context.CancelTransaction();
	}
	if (registered_functions.find(name) != registered_functions.end()) {
		throw NotImplementedException("A function by the name of '%s' is already created, creating multiple "
		                              "functions with the same name is not supported yet, please remove it first",
		                              name);
	}
	auto scalar_function = CreateScalarUDF(name, udf, parameters_p, return_type_p, type == PythonUDFType::ARROW,
	                                       null_handling, exception_handling, side_effects);
	CreateScalarFunctionInfo info(scalar_function);

	context.RegisterFunction(info);

	auto dependency = make_uniq<ExternalDependency>();
	dependency->AddDependency("function", PythonDependencyItem::Create(udf));
	registered_functions[name] = std::move(dependency);

	return shared_from_this();
}

void DuckDBPyConnection::Initialize(py::handle &m) {
	auto connection_module =
	    py::class_<DuckDBPyConnection, shared_ptr<DuckDBPyConnection>>(m, "DuckDBPyConnection", py::module_local());

	connection_module.def("__enter__", &DuckDBPyConnection::Enter)
	    .def("__exit__", &DuckDBPyConnection::Exit, py::arg("exc_type"), py::arg("exc"), py::arg("traceback"));
	connection_module.def("__del__", &DuckDBPyConnection::Close);

	InitializeConnectionMethods(connection_module);
	connection_module.def_property_readonly("description", &DuckDBPyConnection::GetDescription,
	                                        "Get result set attributes, mainly column names");
	connection_module.def_property_readonly("rowcount", &DuckDBPyConnection::GetRowcount, "Get result set row count");
	PyDateTime_IMPORT; // NOLINT
	DuckDBPyConnection::ImportCache();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::ExecuteMany(const py::object &query, py::object params_p) {
	py::gil_scoped_acquire gil;
	con.SetResult(nullptr);
	if (params_p.is_none()) {
		params_p = py::list();
	}

	auto statements = GetStatements(query);
	if (statements.empty()) {
		// TODO: should we throw?
		return nullptr;
	}

	auto last_statement = std::move(statements.back());
	statements.pop_back();
	// First immediately execute any preceding statements (if any)
	// FIXME: DBAPI says to not accept an 'executemany' call with multiple statements
	ExecuteImmediately(std::move(statements));

	auto prep = PrepareQuery(std::move(last_statement));

	if (!py::is_list_like(params_p)) {
		throw InvalidInputException("executemany requires a list of parameter sets to be provided");
	}
	auto outer_list = py::list(params_p);
	if (outer_list.empty()) {
		throw InvalidInputException("executemany requires a non-empty list of parameter sets to be provided");
	}

	unique_ptr<QueryResult> query_result;
	// Execute once for every set of parameters that are provided
	for (auto &parameters : outer_list) {
		auto params = py::reinterpret_borrow<py::object>(parameters);
		query_result = ExecuteInternal(*prep, std::move(params));
	}
	// Set the internal 'result' object
	if (query_result) {
		auto py_result = make_uniq<DuckDBPyResult>(std::move(query_result));
		con.SetResult(make_uniq<DuckDBPyRelation>(std::move(py_result)));
	}

	return shared_from_this();
}

unique_ptr<QueryResult> DuckDBPyConnection::CompletePendingQuery(PendingQueryResult &pending_query) {
	PendingExecutionResult execution_result;
	while (!PendingQueryResult::IsResultReady(execution_result = pending_query.ExecuteTask())) {
		{
			py::gil_scoped_acquire gil;
			if (PyErr_CheckSignals() != 0) {
				throw std::runtime_error("Query interrupted");
			}
		}
		if (execution_result == PendingExecutionResult::BLOCKED) {
			pending_query.WaitForTask();
		}
	}
	if (execution_result == PendingExecutionResult::EXECUTION_ERROR) {
		pending_query.ThrowError();
	}
	return pending_query.Execute();
}

py::list TransformNamedParameters(const case_insensitive_map_t<idx_t> &named_param_map, const py::dict &params) {
	py::list new_params(params.size());

	for (auto &item : params) {
		const std::string &item_name = item.first.cast<std::string>();
		auto entry = named_param_map.find(item_name);
		if (entry == named_param_map.end()) {
			throw InvalidInputException(
			    "Named parameters could not be transformed, because query string is missing named parameter '%s'",
			    item_name);
		}
		auto param_idx = entry->second;
		// Add the value of the named parameter to the list
		new_params[param_idx - 1] = item.second;
	}

	if (named_param_map.size() != params.size()) {
		// One or more named parameters were expected, but not found
		vector<string> missing_params;
		missing_params.reserve(named_param_map.size());
		for (auto &entry : named_param_map) {
			auto &name = entry.first;
			if (!params.contains(name)) {
				missing_params.push_back(name);
			}
		}
		auto message = StringUtil::Join(missing_params, ", ");
		throw InvalidInputException("Not all named parameters have been located, missing: %s", message);
	}

	return new_params;
}

case_insensitive_map_t<BoundParameterData> TransformPreparedParameters(const py::object &params,
                                                                       optional_ptr<PreparedStatement> prep = {}) {
	case_insensitive_map_t<BoundParameterData> named_values;
	if (py::is_list_like(params)) {
		if (prep && prep->named_param_map.size() != py::len(params)) {
			if (py::len(params) == 0) {
				throw InvalidInputException("Expected %d parameters, but none were supplied",
				                            prep->named_param_map.size());
			}
			throw InvalidInputException("Prepared statement needs %d parameters, %d given",
			                            prep->named_param_map.size(), py::len(params));
		}
		auto unnamed_values = DuckDBPyConnection::TransformPythonParamList(params);
		for (idx_t i = 0; i < unnamed_values.size(); i++) {
			auto &value = unnamed_values[i];
			auto identifier = std::to_string(i + 1);
			named_values[identifier] = BoundParameterData(std::move(value));
		}
	} else if (py::is_dict_like(params)) {
		auto dict = py::cast<py::dict>(params);
		named_values = DuckDBPyConnection::TransformPythonParamDict(dict);
	} else {
		throw InvalidInputException("Prepared parameters can only be passed as a list or a dictionary");
	}
	return named_values;
}

unique_ptr<PreparedStatement> DuckDBPyConnection::PrepareQuery(unique_ptr<SQLStatement> statement) {
	auto &connection = con.GetConnection();
	unique_ptr<PreparedStatement> prep;
	{
		D_ASSERT(py::gil_check());
		py::gil_scoped_release release;
		unique_lock<mutex> lock(py_connection_lock);

		prep = connection.Prepare(std::move(statement));
		if (prep->HasError()) {
			prep->error.Throw();
		}
	}
	return prep;
}

unique_ptr<QueryResult> DuckDBPyConnection::ExecuteInternal(PreparedStatement &prep, py::object params) {
	if (params.is_none()) {
		params = py::list();
	}

	// Execute the prepared statement with the prepared parameters
	auto named_values = TransformPreparedParameters(params, prep);
	unique_ptr<QueryResult> res;
	{
		D_ASSERT(py::gil_check());
		py::gil_scoped_release release;
		unique_lock<std::mutex> lock(py_connection_lock);

		auto pending_query = prep.PendingQuery(named_values);
		if (pending_query->HasError()) {
			pending_query->ThrowError();
		}
		res = CompletePendingQuery(*pending_query);

		if (res->HasError()) {
			res->ThrowError();
		}
	}
	return res;
}

unique_ptr<QueryResult> DuckDBPyConnection::PrepareAndExecuteInternal(unique_ptr<SQLStatement> statement,
                                                                      py::object params) {
	if (params.is_none()) {
		params = py::list();
	}

	auto named_values = TransformPreparedParameters(params);

	unique_ptr<QueryResult> res;
	{
		D_ASSERT(py::gil_check());
		py::gil_scoped_release release;
		unique_lock<std::mutex> lock(py_connection_lock);

		auto pending_query = con.GetConnection().PendingQuery(std::move(statement), named_values, true);

		if (pending_query->HasError()) {
			pending_query->ThrowError();
		}

		res = CompletePendingQuery(*pending_query);

		if (res->HasError()) {
			res->ThrowError();
		}
	}
	return res;
}

vector<unique_ptr<SQLStatement>> DuckDBPyConnection::GetStatements(const py::object &query) {
	vector<unique_ptr<SQLStatement>> result;
	auto &connection = con.GetConnection();

	shared_ptr<DuckDBPyStatement> statement_obj;
	if (py::try_cast(query, statement_obj)) {
		result.push_back(statement_obj->GetStatement());
		return result;
	}
	if (py::isinstance<py::str>(query)) {
		auto sql_query = std::string(py::str(query));
		return connection.ExtractStatements(sql_query);
	}
	throw InvalidInputException("Please provide either a DuckDBPyStatement or a string representing the query");
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::ExecuteFromString(const string &query) {
	return Execute(py::str(query));
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Execute(const py::object &query, py::object params) {
	py::gil_scoped_acquire gil;
	con.SetResult(nullptr);

	auto statements = GetStatements(query);
	if (statements.empty()) {
		// TODO: should we throw?
		return nullptr;
	}

	auto last_statement = std::move(statements.back());
	statements.pop_back();
	// First immediately execute any preceding statements (if any)
	// FIXME: SQLites implementation says to not accept an 'execute' call with multiple statements
	ExecuteImmediately(std::move(statements));

	auto res = PrepareAndExecuteInternal(std::move(last_statement), std::move(params));

	// Set the internal 'result' object
	if (res) {
		auto py_result = make_uniq<DuckDBPyResult>(std::move(res));
		con.SetResult(make_uniq<DuckDBPyRelation>(std::move(py_result)));
	}
	return shared_from_this();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Append(const string &name, const PandasDataFrame &value,
                                                          bool by_name) {
	RegisterPythonObject("__append_df", value);
	string columns = "";
	if (by_name) {
		auto df_columns = value.attr("columns");
		vector<string> column_names;
		for (auto &column : df_columns) {
			column_names.push_back(std::string(py::str(column)));
		}
		columns += "(";
		for (idx_t i = 0; i < column_names.size(); i++) {
			auto &column = column_names[i];
			if (i != 0) {
				columns += ", ";
			}
			columns += StringUtil::Format("%s", SQLIdentifier(column));
		}
		columns += ")";
	}

	auto sql_query = StringUtil::Format("INSERT INTO %s %s SELECT * FROM __append_df", SQLIdentifier(name), columns);
	return Execute(py::str(sql_query));
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::RegisterPythonObject(const string &name,
                                                                        const py::object &python_object) {
	auto &connection = con.GetConnection();
	auto &client = *connection.context;
	auto object = PythonReplacementScan::ReplacementObject(python_object, name, client);
	auto view_rel = make_shared_ptr<ViewRelation>(connection.context, std::move(object), name);
	bool replace = registered_objects.count(name);
	view_rel->CreateView(name, replace, true);
	registered_objects.insert(name);
	return shared_from_this();
}

static void ParseMultiFileOptions(named_parameter_map_t &options, const Optional<py::object> &filename,
                                  const Optional<py::object> &hive_partitioning,
                                  const Optional<py::object> &union_by_name, const Optional<py::object> &hive_types,
                                  const Optional<py::object> &hive_types_autocast) {
	if (!py::none().is(filename)) {
		auto val = TransformPythonValue(filename);
		options["filename"] = val;
	}

	if (!py::none().is(hive_types)) {
		auto val = TransformPythonValue(hive_types);
		options["hive_types"] = val;
	}

	if (!py::none().is(hive_partitioning)) {
		if (!py::isinstance<py::bool_>(hive_partitioning)) {
			string actual_type = py::str(hive_partitioning.get_type());
			throw BinderException("read_json only accepts 'hive_partitioning' as a boolean, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(hive_partitioning, LogicalTypeId::BOOLEAN);
		options["hive_partitioning"] = val;
	}

	if (!py::none().is(union_by_name)) {
		if (!py::isinstance<py::bool_>(union_by_name)) {
			string actual_type = py::str(union_by_name.get_type());
			throw BinderException("read_json only accepts 'union_by_name' as a boolean, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(union_by_name, LogicalTypeId::BOOLEAN);
		options["union_by_name"] = val;
	}

	if (!py::none().is(hive_types_autocast)) {
		if (!py::isinstance<py::bool_>(hive_types_autocast)) {
			string actual_type = py::str(hive_types_autocast.get_type());
			throw BinderException("read_json only accepts 'hive_types_autocast' as a boolean, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(hive_types_autocast, LogicalTypeId::BOOLEAN);
		options["hive_types_autocast"] = val;
	}
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::ReadJSON(
    const py::object &name_p, const Optional<py::object> &columns, const Optional<py::object> &sample_size,
    const Optional<py::object> &maximum_depth, const Optional<py::str> &records, const Optional<py::str> &format,
    const Optional<py::object> &date_format, const Optional<py::object> &timestamp_format,
    const Optional<py::object> &compression, const Optional<py::object> &maximum_object_size,
    const Optional<py::object> &ignore_errors, const Optional<py::object> &convert_strings_to_integers,
    const Optional<py::object> &field_appearance_threshold, const Optional<py::object> &map_inference_threshold,
    const Optional<py::object> &maximum_sample_files, const Optional<py::object> &filename,
    const Optional<py::object> &hive_partitioning, const Optional<py::object> &union_by_name,
    const Optional<py::object> &hive_types, const Optional<py::object> &hive_types_autocast) {

	named_parameter_map_t options;

	auto &connection = con.GetConnection();
	auto path_like = GetPathLike(name_p);
	auto &name = path_like.files;
	auto file_like_object_wrapper = std::move(path_like.dependency);

	ParseMultiFileOptions(options, filename, hive_partitioning, union_by_name, hive_types, hive_types_autocast);

	if (!py::none().is(columns)) {
		if (!py::is_dict_like(columns)) {
			throw BinderException("read_json only accepts 'columns' as a dict[str, str]");
		}
		py::dict columns_dict = columns;
		child_list_t<Value> struct_fields;

		for (auto &kv : columns_dict) {
			auto &column_name = kv.first;
			auto &type = kv.second;
			if (!py::isinstance<py::str>(column_name)) {
				string actual_type = py::str(column_name.get_type());
				throw BinderException("The provided column name must be a str, not of type '%s'", actual_type);
			}
			if (!py::isinstance<py::str>(type)) {
				string actual_type = py::str(column_name.get_type());
				throw BinderException("The provided column type must be a str, not of type '%s'", actual_type);
			}
			struct_fields.emplace_back(py::str(column_name), Value(py::str(type)));
		}
		auto dtype_struct = Value::STRUCT(std::move(struct_fields));
		options["columns"] = std::move(dtype_struct);
	}

	if (!py::none().is(records)) {
		if (!py::isinstance<py::str>(records)) {
			string actual_type = py::str(records.get_type());
			throw BinderException("read_json only accepts 'records' as a string, not '%s'", actual_type);
		}
		auto records_s = py::reinterpret_borrow<py::str>(records);
		auto records_option = std::string(py::str(records_s));
		options["records"] = Value(records_option);
	}

	if (!py::none().is(format)) {
		if (!py::isinstance<py::str>(format)) {
			string actual_type = py::str(format.get_type());
			throw BinderException("read_json only accepts 'format' as a string, not '%s'", actual_type);
		}
		auto format_s = py::reinterpret_borrow<py::str>(format);
		auto format_option = std::string(py::str(format_s));
		options["format"] = Value(format_option);
	}

	if (!py::none().is(date_format)) {
		if (!py::isinstance<py::str>(date_format)) {
			string actual_type = py::str(date_format.get_type());
			throw BinderException("read_json only accepts 'date_format' as a string, not '%s'", actual_type);
		}
		auto date_format_s = py::reinterpret_borrow<py::str>(date_format);
		auto date_format_option = std::string(py::str(date_format_s));
		options["date_format"] = Value(date_format_option);
	}

	if (!py::none().is(timestamp_format)) {
		if (!py::isinstance<py::str>(timestamp_format)) {
			string actual_type = py::str(timestamp_format.get_type());
			throw BinderException("read_json only accepts 'timestamp_format' as a string, not '%s'", actual_type);
		}
		auto timestamp_format_s = py::reinterpret_borrow<py::str>(timestamp_format);
		auto timestamp_format_option = std::string(py::str(timestamp_format_s));
		options["timestamp_format"] = Value(timestamp_format_option);
	}

	if (!py::none().is(compression)) {
		if (!py::isinstance<py::str>(compression)) {
			string actual_type = py::str(compression.get_type());
			throw BinderException("read_json only accepts 'compression' as a string, not '%s'", actual_type);
		}
		auto compression_s = py::reinterpret_borrow<py::str>(compression);
		auto compression_option = std::string(py::str(compression_s));
		options["compression"] = Value(compression_option);
	}

	if (!py::none().is(sample_size)) {
		if (!py::isinstance<py::int_>(sample_size)) {
			string actual_type = py::str(sample_size.get_type());
			throw BinderException("read_json only accepts 'sample_size' as an integer, not '%s'", actual_type);
		}
		options["sample_size"] = Value::INTEGER(py::int_(sample_size));
	}

	if (!py::none().is(maximum_depth)) {
		if (!py::isinstance<py::int_>(maximum_depth)) {
			string actual_type = py::str(maximum_depth.get_type());
			throw BinderException("read_json only accepts 'maximum_depth' as an integer, not '%s'", actual_type);
		}
		options["maximum_depth"] = Value::INTEGER(py::int_(maximum_depth));
	}

	if (!py::none().is(maximum_object_size)) {
		if (!py::isinstance<py::int_>(maximum_object_size)) {
			string actual_type = py::str(maximum_object_size.get_type());
			throw BinderException("read_json only accepts 'maximum_object_size' as an unsigned integer, not '%s'",
			                      actual_type);
		}
		auto val = TransformPythonValue(maximum_object_size, LogicalTypeId::UINTEGER);
		options["maximum_object_size"] = val;
	}

	if (!py::none().is(ignore_errors)) {
		if (!py::isinstance<py::bool_>(ignore_errors)) {
			string actual_type = py::str(ignore_errors.get_type());
			throw BinderException("read_json only accepts 'ignore_errors' as a boolean, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(ignore_errors, LogicalTypeId::BOOLEAN);
		options["ignore_errors"] = val;
	}

	if (!py::none().is(convert_strings_to_integers)) {
		if (!py::isinstance<py::bool_>(convert_strings_to_integers)) {
			string actual_type = py::str(convert_strings_to_integers.get_type());
			throw BinderException("read_json only accepts 'convert_strings_to_integers' as a boolean, not '%s'",
			                      actual_type);
		}
		auto val = TransformPythonValue(convert_strings_to_integers, LogicalTypeId::BOOLEAN);
		options["convert_strings_to_integers"] = val;
	}

	if (!py::none().is(field_appearance_threshold)) {
		if (!py::isinstance<py::float_>(field_appearance_threshold)) {
			string actual_type = py::str(field_appearance_threshold.get_type());
			throw BinderException("read_json only accepts 'field_appearance_threshold' as a float, not '%s'",
			                      actual_type);
		}
		auto val = TransformPythonValue(field_appearance_threshold, LogicalTypeId::DOUBLE);
		options["field_appearance_threshold"] = val;
	}

	if (!py::none().is(map_inference_threshold)) {
		if (!py::isinstance<py::int_>(map_inference_threshold)) {
			string actual_type = py::str(map_inference_threshold.get_type());
			throw BinderException("read_json only accepts 'map_inference_threshold' as an integer, not '%s'",
			                      actual_type);
		}
		auto val = TransformPythonValue(map_inference_threshold, LogicalTypeId::BIGINT);
		options["map_inference_threshold"] = val;
	}

	if (!py::none().is(maximum_sample_files)) {
		if (!py::isinstance<py::int_>(maximum_sample_files)) {
			string actual_type = py::str(maximum_sample_files.get_type());
			throw BinderException("read_json only accepts 'maximum_sample_files' as an integer, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(maximum_sample_files, LogicalTypeId::BIGINT);
		options["maximum_sample_files"] = val;
	}

	bool auto_detect = false;
	if (!options.count("columns")) {
		options["auto_detect"] = Value::BOOLEAN(true);
		auto_detect = true;
	}

	D_ASSERT(py::gil_check());
	py::gil_scoped_release gil;
	auto read_json_relation =
	    make_shared_ptr<ReadJSONRelation>(connection.context, name, std::move(options), auto_detect);
	if (read_json_relation == nullptr) {
		throw BinderException("read_json can only be used when the JSON extension is (statically) loaded");
	}
	if (file_like_object_wrapper) {
		read_json_relation->AddExternalDependency(std::move(file_like_object_wrapper));
	}
	return make_uniq<DuckDBPyRelation>(std::move(read_json_relation));
}

PathLike DuckDBPyConnection::GetPathLike(const py::object &object) {
	return PathLike::Create(object, *this);
}

static void AcceptableCSVOptions(const string &unkown_parameter) {
	// List of strings to match against
	const unordered_set<string> valid_parameters = {"header",
	                                                "strict_mode",
	                                                "compression",
	                                                "comment"
	                                                "sep",
	                                                "delimiter",
	                                                "files_to_sniff",
	                                                "dtype",
	                                                "na_values",
	                                                "skiprows",
	                                                "quotechar",
	                                                "escapechar",
	                                                "encoding",
	                                                "parallel",
	                                                "date_format",
	                                                "timestamp_format",
	                                                "sample_size",
	                                                "all_varchar",
	                                                "normalize_names",
	                                                "null_padding",
	                                                "names",
	                                                "lineterminator",
	                                                "columns",
	                                                "auto_type_candidates",
	                                                "max_line_size",
	                                                "ignore_errors",
	                                                "store_rejects",
	                                                "rejects_table",
	                                                "rejects_scan",
	                                                "rejects_limit",
	                                                "force_not_null",
	                                                "buffer_size",
	                                                "decimal",
	                                                "allow_quoted_nulls",
	                                                "filename",
	                                                "hive_partitioning",
	                                                "union_by_name",
	                                                "hive_types",
	                                                "hive_types_autocast",
	                                                "thousands"};

	std::ostringstream error;
	error << "The methods read_csv and read_csv_auto do not have the \"" << unkown_parameter << "\" argument." << '\n';
	error << "Possible arguments as suggestions: " << '\n';
	vector<string> parameters(valid_parameters.begin(), valid_parameters.end());
	auto suggestions = StringUtil::TopNJaroWinkler(parameters, unkown_parameter, 3);
	for (auto &suggestion : suggestions) {
		error << "* " << suggestion << '\n';
	}
	throw InvalidInputException(error.str());
}
void ConvertBooleanValue(const py::object &value, string param_name, named_parameter_map_t &bind_parameters) {
	if (!py::none().is(value)) {

		bool value_as_int = py::isinstance<py::int_>(value);
		bool value_as_bool = py::isinstance<py::bool_>(value);

		bool converted_value;
		if (value_as_bool) {
			converted_value = py::bool_(value);
		} else if (value_as_int) {
			if (static_cast<int>(py::int_(value)) != 0) {
				throw InvalidInputException("read_csv only accepts 0 if '%s' is given as an integer", param_name);
			}
			converted_value = true;
		} else {
			throw InvalidInputException("read_csv only accepts '%s' as an integer, or a boolean", param_name);
		}
		bind_parameters[param_name] = Value::BOOLEAN(converted_value);
	}
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::ReadCSV(const py::object &name_p, py::kwargs &kwargs) {
	py::object header = py::none();
	py::object strict_mode = py::none();
	py::object auto_detect = py::none();
	py::object compression = py::none();
	py::object sep = py::none();
	py::object delimiter = py::none();
	py::object files_to_sniff = py::none();
	py::object dtype = py::none();
	py::object na_values = py::none();
	py::object skiprows = py::none();
	py::object quotechar = py::none();
	py::object escapechar = py::none();
	py::object encoding = py::none();
	py::object parallel = py::none();
	py::object date_format = py::none();
	py::object timestamp_format = py::none();
	py::object sample_size = py::none();
	py::object all_varchar = py::none();
	py::object normalize_names = py::none();
	py::object null_padding = py::none();
	py::object names_p = py::none();
	py::object lineterminator = py::none();
	py::object columns = py::none();
	py::object auto_type_candidates = py::none();
	py::object max_line_size = py::none();
	py::object ignore_errors = py::none();
	py::object store_rejects = py::none();
	py::object rejects_table = py::none();
	py::object rejects_scan = py::none();
	py::object rejects_limit = py::none();
	py::object force_not_null = py::none();
	py::object buffer_size = py::none();
	py::object decimal = py::none();
	py::object allow_quoted_nulls = py::none();
	py::object filename = py::none();
	py::object hive_partitioning = py::none();
	py::object union_by_name = py::none();
	py::object hive_types = py::none();
	py::object hive_types_autocast = py::none();
	py::object comment = py::none();
	py::object thousands_separator = py::none();

	for (auto &arg : kwargs) {
		const auto &arg_name = py::str(arg.first).cast<std::string>();
		if (arg_name == "header") {
			header = kwargs[arg_name.c_str()];
		} else if (arg_name == "compression") {
			compression = kwargs[arg_name.c_str()];
		} else if (arg_name == "sep") {
			sep = kwargs[arg_name.c_str()];
		} else if (arg_name == "delimiter") {
			delimiter = kwargs[arg_name.c_str()];
		} else if (arg_name == "files_to_sniff") {
			files_to_sniff = kwargs[arg_name.c_str()];
		} else if (arg_name == "comment") {
			comment = kwargs[arg_name.c_str()];
		} else if (arg_name == "thousands") {
			thousands_separator = kwargs[arg_name.c_str()];
		} else if (arg_name == "dtype") {
			dtype = kwargs[arg_name.c_str()];
		} else if (arg_name == "na_values") {
			na_values = kwargs[arg_name.c_str()];
		} else if (arg_name == "skiprows") {
			skiprows = kwargs[arg_name.c_str()];
		} else if (arg_name == "quotechar") {
			quotechar = kwargs[arg_name.c_str()];
		} else if (arg_name == "escapechar") {
			escapechar = kwargs[arg_name.c_str()];
		} else if (arg_name == "encoding") {
			encoding = kwargs[arg_name.c_str()];
		} else if (arg_name == "parallel") {
			parallel = kwargs[arg_name.c_str()];
		} else if (arg_name == "date_format") {
			date_format = kwargs[arg_name.c_str()];
		} else if (arg_name == "timestamp_format") {
			timestamp_format = kwargs[arg_name.c_str()];
		} else if (arg_name == "sample_size") {
			sample_size = kwargs[arg_name.c_str()];
		} else if (arg_name == "auto_detect") {
			auto_detect = kwargs[arg_name.c_str()];
		} else if (arg_name == "all_varchar") {
			all_varchar = kwargs[arg_name.c_str()];
		} else if (arg_name == "normalize_names") {
			normalize_names = kwargs[arg_name.c_str()];
		} else if (arg_name == "null_padding") {
			null_padding = kwargs[arg_name.c_str()];
		} else if (arg_name == "names") {
			names_p = kwargs[arg_name.c_str()];
		} else if (arg_name == "lineterminator") {
			lineterminator = kwargs[arg_name.c_str()];
		} else if (arg_name == "columns") {
			columns = kwargs[arg_name.c_str()];
		} else if (arg_name == "auto_type_candidates") {
			auto_type_candidates = kwargs[arg_name.c_str()];
		} else if (arg_name == "max_line_size") {
			max_line_size = kwargs[arg_name.c_str()];
		} else if (arg_name == "ignore_errors") {
			ignore_errors = kwargs[arg_name.c_str()];
		} else if (arg_name == "store_rejects") {
			store_rejects = kwargs[arg_name.c_str()];
		} else if (arg_name == "rejects_table") {
			rejects_table = kwargs[arg_name.c_str()];
		} else if (arg_name == "rejects_scan") {
			rejects_scan = kwargs[arg_name.c_str()];
		} else if (arg_name == "rejects_limit") {
			rejects_limit = kwargs[arg_name.c_str()];
		} else if (arg_name == "force_not_null") {
			force_not_null = kwargs[arg_name.c_str()];
		} else if (arg_name == "buffer_size") {
			buffer_size = kwargs[arg_name.c_str()];
		} else if (arg_name == "decimal") {
			decimal = kwargs[arg_name.c_str()];
		} else if (arg_name == "allow_quoted_nulls") {
			allow_quoted_nulls = kwargs[arg_name.c_str()];
		} else if (arg_name == "filename") {
			filename = kwargs[arg_name.c_str()];
		} else if (arg_name == "hive_partitioning") {
			hive_partitioning = kwargs[arg_name.c_str()];
		} else if (arg_name == "union_by_name") {
			union_by_name = kwargs[arg_name.c_str()];
		} else if (arg_name == "hive_types") {
			hive_types = kwargs[arg_name.c_str()];
		} else if (arg_name == "hive_types_autocast") {
			hive_types_autocast = kwargs[arg_name.c_str()];
		} else if (arg_name == "strict_mode") {
			strict_mode = kwargs[arg_name.c_str()];
		} else {
			AcceptableCSVOptions(arg_name);
		}
	}

	auto &connection = con.GetConnection();
	CSVReaderOptions options;
	auto path_like = GetPathLike(name_p);
	auto &name = path_like.files;
	auto file_like_object_wrapper = std::move(path_like.dependency);
	named_parameter_map_t bind_parameters;

	ParseMultiFileOptions(bind_parameters, filename, hive_partitioning, union_by_name, hive_types, hive_types_autocast);

	// First check if the header is explicitly set
	// when false this affects the returned types, so it needs to be known at initialization of the relation
	ConvertBooleanValue(header, "header", bind_parameters);
	ConvertBooleanValue(strict_mode, "strict_mode", bind_parameters);

	if (!py::none().is(compression)) {
		if (!py::isinstance<py::str>(compression)) {
			throw InvalidInputException("read_csv only accepts 'compression' as a string");
		}
		bind_parameters["compression"] = Value(py::str(compression));
	}

	if (!py::none().is(dtype)) {
		if (py::is_dict_like(dtype)) {
			child_list_t<Value> struct_fields;
			py::dict dtype_dict = dtype;
			for (auto &kv : dtype_dict) {
				shared_ptr<DuckDBPyType> sql_type;
				if (!py::try_cast(kv.second, sql_type)) {
					struct_fields.emplace_back(py::str(kv.first), py::str(kv.second));
				} else {
					struct_fields.emplace_back(py::str(kv.first), Value(sql_type->ToString()));
				}
			}
			auto dtype_struct = Value::STRUCT(std::move(struct_fields));
			bind_parameters["dtypes"] = std::move(dtype_struct);
		} else if (py::is_list_like(dtype)) {
			vector<Value> list_values;
			py::list dtype_list = dtype;
			for (auto &child : dtype_list) {
				shared_ptr<DuckDBPyType> sql_type;
				if (!py::try_cast(child, sql_type)) {
					list_values.push_back(Value(py::str(child)));
				} else {
					list_values.push_back(sql_type->ToString());
				}
			}
			bind_parameters["dtypes"] = Value::LIST(LogicalType::VARCHAR, std::move(list_values));
		} else {
			throw InvalidInputException("read_csv only accepts 'dtype' as a dictionary or a list of strings");
		}
	}

	bool has_sep = !py::none().is(sep);
	bool has_delimiter = !py::none().is(delimiter);
	if (has_sep && has_delimiter) {
		throw InvalidInputException("read_csv takes either 'delimiter' or 'sep', not both");
	}
	if (has_sep) {
		bind_parameters["delim"] = Value(py::str(sep));
	} else if (has_delimiter) {
		bind_parameters["delim"] = Value(py::str(delimiter));
	}

	if (!py::none().is(files_to_sniff)) {
		if (!py::isinstance<py::int_>(files_to_sniff)) {
			throw InvalidInputException("read_csv only accepts 'files_to_sniff' as an integer");
		}
		bind_parameters["files_to_sniff"] = Value::INTEGER(py::int_(files_to_sniff));
	}

	if (!py::none().is(names_p)) {
		if (!py::is_list_like(names_p)) {
			throw InvalidInputException("read_csv only accepts 'names' as a list of strings");
		}
		vector<Value> names;
		py::list names_list = names_p;
		for (auto &elem : names_list) {
			if (!py::isinstance<py::str>(elem)) {
				throw InvalidInputException("read_csv 'names' list has to consist of only strings");
			}
			names.push_back(Value(std::string(py::str(elem))));
		}
		bind_parameters["names"] = Value::LIST(LogicalType::VARCHAR, std::move(names));
	}

	if (!py::none().is(na_values)) {
		vector<Value> null_values;
		if (!py::isinstance<py::str>(na_values) && !py::is_list_like(na_values)) {
			throw InvalidInputException("read_csv only accepts 'na_values' as a string or a list of strings");
		} else if (py::isinstance<py::str>(na_values)) {
			null_values.push_back(Value(py::str(na_values)));
		} else {
			py::list null_list = na_values;
			for (auto &elem : null_list) {
				if (!py::isinstance<py::str>(elem)) {
					throw InvalidInputException("read_csv 'na_values' list has to consist of only strings");
				}
				null_values.push_back(Value(std::string(py::str(elem))));
			}
		}
		bind_parameters["nullstr"] = Value::LIST(LogicalType::VARCHAR, std::move(null_values));
	}

	if (!py::none().is(skiprows)) {
		if (!py::isinstance<py::int_>(skiprows)) {
			throw InvalidInputException("read_csv only accepts 'skiprows' as an integer");
		}
		bind_parameters["skip"] = Value::INTEGER(py::int_(skiprows));
	}

	if (!py::none().is(parallel)) {
		if (!py::isinstance<py::bool_>(parallel)) {
			throw InvalidInputException("read_csv only accepts 'parallel' as a boolean");
		}
		bind_parameters["parallel"] = Value::BOOLEAN(py::bool_(parallel));
	}

	if (!py::none().is(quotechar)) {
		if (!py::isinstance<py::str>(quotechar)) {
			throw InvalidInputException("read_csv only accepts 'quotechar' as a string");
		}
		bind_parameters["quote"] = Value(py::str(quotechar));
	}

	if (!py::none().is(comment)) {
		if (!py::isinstance<py::str>(comment)) {
			throw InvalidInputException("read_csv only accepts 'comment' as a string");
		}
		bind_parameters["comment"] = Value(py::str(comment));
	}

	if (!py::none().is(thousands_separator)) {
		if (!py::isinstance<py::str>(thousands_separator)) {
			throw InvalidInputException("read_csv only accepts 'thousands' as a string");
		}
		bind_parameters["thousands"] = Value(py::str(thousands_separator));
	}

	if (!py::none().is(escapechar)) {
		if (!py::isinstance<py::str>(escapechar)) {
			throw InvalidInputException("read_csv only accepts 'escapechar' as a string");
		}
		bind_parameters["escape"] = Value(py::str(escapechar));
	}

	if (!py::none().is(encoding)) {
		if (!py::isinstance<py::str>(encoding)) {
			throw InvalidInputException("read_csv only accepts 'encoding' as a string");
		}
		string encoding_str = StringUtil::Lower(py::str(encoding));
		if (encoding_str != "utf8" && encoding_str != "utf-8") {
			throw BinderException("Copy is only supported for UTF-8 encoded files, ENCODING 'UTF-8'");
		}
	}

	if (!py::none().is(date_format)) {
		if (!py::isinstance<py::str>(date_format)) {
			throw InvalidInputException("read_csv only accepts 'date_format' as a string");
		}
		bind_parameters["dateformat"] = Value(py::str(date_format));
	}

	if (!py::none().is(auto_detect)) {
		bool auto_detect_as_int = py::isinstance<py::int_>(auto_detect);
		bool auto_detect_as_bool = py::isinstance<py::bool_>(auto_detect);
		bool auto_detect_value;
		if (auto_detect_as_bool) {
			auto_detect_value = py::bool_(auto_detect);
		} else if (auto_detect_as_int) {
			if ((int)py::int_(auto_detect) != 0) {
				throw InvalidInputException("read_csv only accepts 0 if 'auto_detect' is given as an integer");
			}
			auto_detect_value = true;
		} else {
			throw InvalidInputException("read_csv only accepts 'auto_detect' as an integer, or a boolean");
		}
		bind_parameters["auto_detect"] = Value::BOOLEAN(auto_detect_value);
	}

	if (!py::none().is(timestamp_format)) {
		if (!py::isinstance<py::str>(timestamp_format)) {
			throw InvalidInputException("read_csv only accepts 'timestamp_format' as a string");
		}
		bind_parameters["timestampformat"] = Value(py::str(timestamp_format));
	}

	if (!py::none().is(sample_size)) {
		if (!py::isinstance<py::int_>(sample_size)) {
			throw InvalidInputException("read_csv only accepts 'sample_size' as an integer");
		}
		bind_parameters["sample_size"] = Value::INTEGER(py::int_(sample_size));
	}

	if (!py::none().is(all_varchar)) {
		if (!py::isinstance<py::bool_>(all_varchar)) {
			throw InvalidInputException("read_csv only accepts 'all_varchar' as a boolean");
		}
		bind_parameters["all_varchar"] = Value::BOOLEAN(py::bool_(all_varchar));
	}

	if (!py::none().is(normalize_names)) {
		if (!py::isinstance<py::bool_>(normalize_names)) {
			throw InvalidInputException("read_csv only accepts 'normalize_names' as a boolean");
		}
		bind_parameters["normalize_names"] = Value::BOOLEAN(py::bool_(normalize_names));
	}

	if (!py::none().is(null_padding)) {
		if (!py::isinstance<py::bool_>(null_padding)) {
			throw InvalidInputException("read_csv only accepts 'null_padding' as a boolean");
		}
		bind_parameters["null_padding"] = Value::BOOLEAN(py::bool_(null_padding));
	}

	if (!py::none().is(lineterminator)) {
		PythonCSVLineTerminator::Type new_line_type;
		if (!py::try_cast<PythonCSVLineTerminator::Type>(lineterminator, new_line_type)) {
			string actual_type = py::str(lineterminator.get_type());
			throw BinderException("read_csv only accepts 'lineterminator' as a string or CSVLineTerminator, not '%s'",
			                      actual_type);
		}
		bind_parameters["new_line"] = Value(PythonCSVLineTerminator::ToString(new_line_type));
	}

	if (!py::none().is(max_line_size)) {
		if (!py::isinstance<py::str>(max_line_size) && !py::isinstance<py::int_>(max_line_size)) {
			string actual_type = py::str(max_line_size.get_type());
			throw BinderException("read_csv only accepts 'max_line_size' as a string or an integer, not '%s'",
			                      actual_type);
		}
		auto val = TransformPythonValue(max_line_size, LogicalTypeId::VARCHAR);
		bind_parameters["max_line_size"] = val;
	}

	if (!py::none().is(auto_type_candidates)) {
		if (!py::isinstance<py::list>(auto_type_candidates)) {
			string actual_type = py::str(auto_type_candidates.get_type());
			throw BinderException("read_csv only accepts 'auto_type_candidates' as a list[str], not '%s'", actual_type);
		}
		auto val = TransformPythonValue(auto_type_candidates, LogicalType::LIST(LogicalTypeId::VARCHAR));
		bind_parameters["auto_type_candidates"] = val;
	}

	if (!py::none().is(ignore_errors)) {
		if (!py::isinstance<py::bool_>(ignore_errors)) {
			string actual_type = py::str(ignore_errors.get_type());
			throw BinderException("read_csv only accepts 'ignore_errors' as a bool, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(ignore_errors, LogicalTypeId::BOOLEAN);
		bind_parameters["ignore_errors"] = val;
	}

	if (!py::none().is(store_rejects)) {
		if (!py::isinstance<py::bool_>(store_rejects)) {
			string actual_type = py::str(store_rejects.get_type());
			throw BinderException("read_csv only accepts 'store_rejects' as a bool, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(store_rejects, LogicalTypeId::BOOLEAN);
		bind_parameters["store_rejects"] = val;
	}

	if (!py::none().is(rejects_table)) {
		if (!py::isinstance<py::str>(rejects_table)) {
			string actual_type = py::str(rejects_table.get_type());
			throw BinderException("read_csv only accepts 'rejects_table' as a string, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(rejects_table, LogicalTypeId::VARCHAR);
		bind_parameters["rejects_table"] = val;
	}

	if (!py::none().is(rejects_scan)) {
		if (!py::isinstance<py::str>(rejects_scan)) {
			string actual_type = py::str(rejects_scan.get_type());
			throw BinderException("read_csv only accepts 'rejects_scan' as a string, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(rejects_scan, LogicalTypeId::VARCHAR);
		bind_parameters["rejects_scan"] = val;
	}

	if (!py::none().is(rejects_limit)) {
		if (!py::isinstance<py::int_>(rejects_limit)) {
			string actual_type = py::str(rejects_limit.get_type());
			throw BinderException("read_csv only accepts 'rejects_limit' as an int, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(rejects_limit, LogicalTypeId::BIGINT);
		bind_parameters["rejects_limit"] = val;
	}

	if (!py::none().is(force_not_null)) {
		if (!py::isinstance<py::list>(force_not_null)) {
			string actual_type = py::str(force_not_null.get_type());
			throw BinderException("read_csv only accepts 'force_not_null' as a list[str], not '%s'", actual_type);
		}
		auto val = TransformPythonValue(force_not_null, LogicalType::LIST(LogicalTypeId::VARCHAR));
		bind_parameters["force_not_null"] = val;
	}

	if (!py::none().is(buffer_size)) {
		if (!py::isinstance<py::int_>(buffer_size)) {
			string actual_type = py::str(buffer_size.get_type());
			throw BinderException("read_csv only accepts 'buffer_size' as a list[str], not '%s'", actual_type);
		}
		auto val = TransformPythonValue(buffer_size, LogicalTypeId::UBIGINT);
		bind_parameters["buffer_size"] = val;
	}

	if (!py::none().is(decimal)) {
		if (!py::isinstance<py::str>(decimal)) {
			string actual_type = py::str(decimal.get_type());
			throw BinderException("read_csv only accepts 'decimal' as a string, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(decimal, LogicalTypeId::VARCHAR);
		bind_parameters["decimal_separator"] = val;
	}

	if (!py::none().is(allow_quoted_nulls)) {
		if (!py::isinstance<py::bool_>(allow_quoted_nulls)) {
			string actual_type = py::str(allow_quoted_nulls.get_type());
			throw BinderException("read_csv only accepts 'allow_quoted_nulls' as a bool, not '%s'", actual_type);
		}
		auto val = TransformPythonValue(allow_quoted_nulls, LogicalTypeId::BOOLEAN);
		bind_parameters["allow_quoted_nulls"] = val;
	}

	if (!py::none().is(columns)) {
		if (!py::is_dict_like(columns)) {
			throw BinderException("read_csv only accepts 'columns' as a dict[str, str]");
		}
		py::dict columns_dict = columns;
		child_list_t<Value> struct_fields;

		for (auto &kv : columns_dict) {
			auto &column_name = kv.first;
			auto &type = kv.second;
			if (!py::isinstance<py::str>(column_name)) {
				string actual_type = py::str(column_name.get_type());
				throw BinderException("The provided column name must be a str, not of type '%s'", actual_type);
			}
			if (!py::isinstance<py::str>(type)) {
				string actual_type = py::str(column_name.get_type());
				throw BinderException("The provided column type must be a str, not of type '%s'", actual_type);
			}
			struct_fields.emplace_back(py::str(column_name), Value(py::str(type)));
		}
		auto dtype_struct = Value::STRUCT(std::move(struct_fields));
		bind_parameters["columns"] = std::move(dtype_struct);
	}

	// Create the ReadCSV Relation using the 'options'

	D_ASSERT(py::gil_check());
	py::gil_scoped_release gil;
	auto read_csv_p = connection.ReadCSV(name, std::move(bind_parameters));
	auto &read_csv = read_csv_p->Cast<ReadCSVRelation>();
	if (file_like_object_wrapper) {
		read_csv.AddExternalDependency(std::move(file_like_object_wrapper));
	}

	return make_uniq<DuckDBPyRelation>(read_csv_p->Alias(read_csv.alias));
}

void DuckDBPyConnection::ExecuteImmediately(vector<unique_ptr<SQLStatement>> statements) {
	auto &connection = con.GetConnection();
	D_ASSERT(py::gil_check());
	py::gil_scoped_release release;
	if (statements.empty()) {
		return;
	}
	for (auto &stmt : statements) {
		if (!stmt->named_param_map.empty()) {
			throw NotImplementedException(
			    "Prepared parameters are only supported for the last statement, please split your query up into "
			    "separate 'execute' calls if you want to use prepared parameters");
		}
		auto pending_query = connection.PendingQuery(std::move(stmt), false);
		if (pending_query->HasError()) {
			pending_query->ThrowError();
		}
		auto res = CompletePendingQuery(*pending_query);

		if (res->HasError()) {
			res->ThrowError();
		}
	}
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::RunQuery(const py::object &query, string alias, py::object params) {
	auto &connection = con.GetConnection();
	if (alias.empty()) {
		alias = "unnamed_relation_" + StringUtil::GenerateRandomName(16);
	}

	auto statements = GetStatements(query);
	if (statements.empty()) {
		// TODO: should we throw?
		return nullptr;
	}

	auto last_statement = std::move(statements.back());
	statements.pop_back();
	// First immediately execute any preceding statements (if any)
	ExecuteImmediately(std::move(statements));

	// Attempt to create a Relation for lazy execution if possible
	shared_ptr<Relation> relation;
	if (py::none().is(params)) {
		// FIXME: currently we can't create relations with prepared parameters
		{
			D_ASSERT(py::gil_check());
			py::gil_scoped_release gil;
			auto statement_type = last_statement->type;
			switch (statement_type) {
			case StatementType::SELECT_STATEMENT: {
				auto select_statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(last_statement));
				relation = connection.RelationFromQuery(std::move(select_statement), alias);
				break;
			}
			default:
				break;
			}
		}
	}

	if (!relation) {
		// Could not create a relation, resort to direct execution
		unique_ptr<QueryResult> res;

		res = PrepareAndExecuteInternal(std::move(last_statement), std::move(params));

		if (!res) {
			return nullptr;
		}
		if (res->properties.return_type != StatementReturnType::QUERY_RESULT) {
			return nullptr;
		}
		if (res->type == QueryResultType::STREAM_RESULT) {
			auto &stream_result = res->Cast<StreamQueryResult>();
			res = stream_result.Materialize();
		}
		auto &materialized_result = res->Cast<MaterializedQueryResult>();
		relation = make_shared_ptr<MaterializedRelation>(connection.context, materialized_result.TakeCollection(),
		                                                 res->names, alias);
	}
	return make_uniq<DuckDBPyRelation>(std::move(relation));
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::Table(const string &tname) {
	auto &connection = con.GetConnection();
	auto qualified_name = QualifiedName::Parse(tname);
	if (qualified_name.schema.empty()) {
		qualified_name.schema = DEFAULT_SCHEMA;
	}
	try {
		return make_uniq<DuckDBPyRelation>(
		    connection.Table(qualified_name.catalog, qualified_name.schema, qualified_name.name));
	} catch (const CatalogException &) {
		// CatalogException will be of the type '... is not a table'
		// Not a table in the database, make a query relation that can perform replacement scans
		auto sql_query = StringUtil::Format("from %s", KeywordHelper::WriteOptionallyQuoted(tname));
		return RunQuery(py::str(sql_query), tname);
	}
}

static vector<unique_ptr<ParsedExpression>> ValueListFromExpressions(const py::args &expressions) {
	vector<unique_ptr<ParsedExpression>> result;
	auto arg_count = expressions.size();
	if (arg_count == 0) {
		throw InvalidInputException("Please provide a non-empty tuple");
	}

	for (idx_t i = 0; i < arg_count; i++) {
		py::handle arg = expressions[i];
		shared_ptr<DuckDBPyExpression> py_expr;
		if (!py::try_cast<shared_ptr<DuckDBPyExpression>>(arg, py_expr)) {
			throw InvalidInputException("Please provide arguments of type Expression!");
		}
		auto expr = py_expr->GetExpression().Copy();
		result.push_back(std::move(expr));
	}
	return result;
}

static vector<vector<unique_ptr<ParsedExpression>>> ValueListsFromTuples(const py::args &tuples) {
	auto arg_count = tuples.size();
	if (arg_count == 0) {
		throw InvalidInputException("Please provide a non-empty tuple");
	}

	idx_t expected_length = 0;
	vector<vector<unique_ptr<ParsedExpression>>> result;
	for (idx_t i = 0; i < arg_count; i++) {
		py::handle arg = tuples[i];
		if (!py::isinstance<py::tuple>(arg)) {
			string actual_type = py::str(arg.get_type());
			throw InvalidInputException("Expected objects of type tuple, not %s", actual_type);
		}
		auto expressions = py::cast<py::args>(arg);
		auto value_list = ValueListFromExpressions(expressions);
		if (i && value_list.size() != expected_length) {
			throw InvalidInputException("Mismatch between length of tuples in input, expected %d but found %d",
			                            expected_length, value_list.size());
		}
		expected_length = value_list.size();
		result.push_back(std::move(value_list));
	}
	return result;
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::Values(const py::args &args) {
	auto &connection = con.GetConnection();

	auto arg_count = args.size();
	if (arg_count == 0) {
		throw InvalidInputException("Could not create a ValueRelation without any inputs");
	}

	D_ASSERT(py::gil_check());
	py::handle first_arg = args[0];
	if (arg_count == 1 && py::isinstance<py::list>(first_arg)) {
		vector<vector<Value>> values {DuckDBPyConnection::TransformPythonParamList(first_arg)};
		return make_uniq<DuckDBPyRelation>(connection.Values(values));
	} else {
		vector<vector<unique_ptr<ParsedExpression>>> expressions;
		if (py::isinstance<py::tuple>(first_arg)) {
			expressions = ValueListsFromTuples(args);
		} else {
			auto values = ValueListFromExpressions(args);
			expressions.push_back(std::move(values));
		}
		return make_uniq<DuckDBPyRelation>(connection.Values(std::move(expressions)));
	}
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::View(const string &vname) {
	auto &connection = con.GetConnection();
	return make_uniq<DuckDBPyRelation>(connection.View(vname));
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::TableFunction(const string &fname, py::object params) {
	auto &connection = con.GetConnection();
	if (params.is_none()) {
		params = py::list();
	}
	if (!py::is_list_like(params)) {
		throw InvalidInputException("'params' has to be a list of parameters");
	}

	return make_uniq<DuckDBPyRelation>(
	    connection.TableFunction(fname, DuckDBPyConnection::TransformPythonParamList(params)));
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::FromDF(const PandasDataFrame &value) {
	auto &connection = con.GetConnection();
	string name = "df_" + StringUtil::GenerateRandomName();
	if (PandasDataFrame::IsPyArrowBacked(value)) {
		auto table = PandasDataFrame::ToArrowTable(value);
		return DuckDBPyConnection::FromArrow(table);
	}
	auto tableref = PythonReplacementScan::ReplacementObject(value, name, *connection.context);
	D_ASSERT(tableref);
	auto rel = make_shared_ptr<ViewRelation>(connection.context, std::move(tableref), name);
	return make_uniq<DuckDBPyRelation>(std::move(rel));
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::FromParquetInternal(Value &&file_param, bool binary_as_string,
                                                                     bool file_row_number, bool filename,
                                                                     bool hive_partitioning, bool union_by_name,
                                                                     const py::object &compression) {
	auto &connection = con.GetConnection();
	string name = "parquet_" + StringUtil::GenerateRandomName();
	vector<Value> params;
	params.emplace_back(std::move(file_param));
	named_parameter_map_t named_parameters({{"binary_as_string", Value::BOOLEAN(binary_as_string)},
	                                        {"file_row_number", Value::BOOLEAN(file_row_number)},
	                                        {"filename", Value::BOOLEAN(filename)},
	                                        {"hive_partitioning", Value::BOOLEAN(hive_partitioning)},
	                                        {"union_by_name", Value::BOOLEAN(union_by_name)}});

	if (!py::none().is(compression)) {
		if (!py::isinstance<py::str>(compression)) {
			throw InvalidInputException("from_parquet only accepts 'compression' as a string");
		}
		named_parameters["compression"] = Value(py::str(compression));
	}
	D_ASSERT(py::gil_check());
	py::gil_scoped_release gil;
	return make_uniq<DuckDBPyRelation>(connection.TableFunction("parquet_scan", params, named_parameters)->Alias(name));
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::FromParquet(const string &file_glob, bool binary_as_string,
                                                             bool file_row_number, bool filename,
                                                             bool hive_partitioning, bool union_by_name,
                                                             const py::object &compression) {
	auto file_param = Value(file_glob);
	return FromParquetInternal(std::move(file_param), binary_as_string, file_row_number, filename, hive_partitioning,
	                           union_by_name, compression);
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::FromParquets(const vector<string> &file_globs, bool binary_as_string,
                                                              bool file_row_number, bool filename,
                                                              bool hive_partitioning, bool union_by_name,
                                                              const py::object &compression) {
	vector<Value> params;
	auto file_globs_as_value = vector<Value>();
	for (const auto &file : file_globs) {
		file_globs_as_value.emplace_back(file);
	}
	auto file_param = Value::LIST(file_globs_as_value);
	return FromParquetInternal(std::move(file_param), binary_as_string, file_row_number, filename, hive_partitioning,
	                           union_by_name, compression);
}

unique_ptr<DuckDBPyRelation> DuckDBPyConnection::FromArrow(py::object &arrow_object) {
	auto &connection = con.GetConnection();
	string name = "arrow_object_" + StringUtil::GenerateRandomName();
	if (!IsAcceptedArrowObject(arrow_object)) {
		auto py_object_type = string(py::str(arrow_object.get_type().attr("__name__")));
		throw InvalidInputException("Python Object Type %s is not an accepted Arrow Object.", py_object_type);
	}
	auto tableref = PythonReplacementScan::ReplacementObject(arrow_object, name, *connection.context, true);
	D_ASSERT(tableref);
	auto rel = make_shared_ptr<ViewRelation>(connection.context, std::move(tableref), name);
	return make_uniq<DuckDBPyRelation>(std::move(rel));
}

unordered_set<string> DuckDBPyConnection::GetTableNames(const string &query, bool qualified) {
	auto &connection = con.GetConnection();
	return connection.GetTableNames(query, qualified);
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::UnregisterPythonObject(const string &name) {
	auto &connection = con.GetConnection();
	if (!registered_objects.count(name)) {
		return shared_from_this();
	}
	D_ASSERT(py::gil_check());
	py::gil_scoped_release release;
	// FIXME: DROP TEMPORARY VIEW? doesn't exist?
	connection.Query("DROP VIEW \"" + name + "\"");
	registered_objects.erase(name);
	return shared_from_this();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Begin() {
	ExecuteFromString("BEGIN TRANSACTION");
	return shared_from_this();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Commit() {
	auto &connection = con.GetConnection();
	if (connection.context->transaction.IsAutoCommit()) {
		return shared_from_this();
	}
	ExecuteFromString("COMMIT");
	return shared_from_this();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Rollback() {
	ExecuteFromString("ROLLBACK");
	return shared_from_this();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Checkpoint() {
	ExecuteFromString("CHECKPOINT");
	return shared_from_this();
}

Optional<py::list> DuckDBPyConnection::GetDescription() {
	if (!con.HasResult()) {
		return py::none();
	}
	auto &result = con.GetResult();
	return result.Description();
}

int DuckDBPyConnection::GetRowcount() {
	return -1;
}

void DuckDBPyConnection::Close() {
	con.SetResult(nullptr);
	D_ASSERT(py::gil_check());
	py::gil_scoped_release release;
	con.SetConnection(nullptr);
	con.SetDatabase(nullptr);
	// https://peps.python.org/pep-0249/#Connection.close
	cursors.ClearCursors();
	registered_functions.clear();
}

void DuckDBPyConnection::Interrupt() {
	auto &connection = con.GetConnection();
	connection.Interrupt();
}

double DuckDBPyConnection::QueryProgress() {
	auto &connection = con.GetConnection();
	return connection.GetQueryProgress();
}

void DuckDBPyConnection::InstallExtension(const string &extension, bool force_install, const py::object &repository,
                                          const py::object &repository_url, const py::object &version) {
	auto &connection = con.GetConnection();

	auto install_statement = make_uniq<LoadStatement>();
	install_statement->info = make_uniq<LoadInfo>();
	auto &info = *install_statement->info;

	info.filename = extension;

	const bool has_repository = !py::none().is(repository);
	const bool has_repository_url = !py::none().is(repository_url);
	if (has_repository && has_repository_url) {
		throw InvalidInputException(
		    "Both 'repository' and 'repository_url' are set which is not allowed, please pick one or the other");
	}
	string repository_string;
	if (has_repository) {
		repository_string = py::str(repository);
	} else if (has_repository_url) {
		repository_string = py::str(repository_url);
	}

	if ((has_repository || has_repository_url) && repository_string.empty()) {
		throw InvalidInputException("The provided 'repository' or 'repository_url' can not be empty!");
	}

	string version_string;
	if (!py::none().is(version)) {
		version_string = py::str(version);
		if (version_string.empty()) {
			throw InvalidInputException("The provided 'version' can not be empty!");
		}
	}

	info.repository = repository_string;
	info.repo_is_alias = repository_string.empty() ? false : has_repository;
	info.version = version_string;
	info.load_type = force_install ? LoadType::FORCE_INSTALL : LoadType::INSTALL;
	auto res = connection.Query(std::move(install_statement));
	if (res->HasError()) {
		res->ThrowError();
	}
}

void DuckDBPyConnection::LoadExtension(const string &extension) {
	auto &connection = con.GetConnection();
	ExtensionHelper::LoadExternalExtension(*connection.context, extension);
}

shared_ptr<DuckDBPyConnection> DefaultConnectionHolder::Get() {
	lock_guard<mutex> guard(l);
	if (!connection || connection->con.ConnectionIsClosed()) {
		py::dict config_dict;
		connection = DuckDBPyConnection::Connect(py::str(":memory:"), false, config_dict);
	}
	return connection;
}

void DefaultConnectionHolder::Set(shared_ptr<DuckDBPyConnection> conn) {
	lock_guard<mutex> guard(l);
	connection = conn;
}

void DuckDBPyConnection::Cursors::AddCursor(shared_ptr<DuckDBPyConnection> conn) {
	lock_guard<mutex> l(lock);

	// Clean up previously created cursors
	vector<weak_ptr<DuckDBPyConnection>> compacted_cursors;
	bool needs_compaction = false;
	for (auto &cur_p : cursors) {
		auto cur = cur_p.lock();
		if (!cur) {
			needs_compaction = true;
			continue;
		}
		compacted_cursors.push_back(cur_p);
	}
	if (needs_compaction) {
		cursors = std::move(compacted_cursors);
	}

	cursors.push_back(conn);
}

void DuckDBPyConnection::Cursors::ClearCursors() {
	lock_guard<mutex> l(lock);

	for (auto &cur : cursors) {
		auto cursor = cur.lock();
		if (!cursor) {
			// The cursor has already been closed
			continue;
		}
		// This is *only* needed because we have a py::gil_scoped_release in Close, so it *needs* the GIL in order to
		// release it don't ask me why it can't just realize there is no GIL and move on
		py::gil_scoped_acquire gil;
		cursor->Close();
		// Ensure destructor runs with gil if triggered.
		cursor.reset();
	}

	cursors.clear();
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Cursor() {
	auto res = make_shared_ptr<DuckDBPyConnection>();
	res->con.SetDatabase(con);
	res->con.SetConnection(make_uniq<Connection>(res->con.GetDatabase()));
	cursors.AddCursor(res);
	return res;
}

// these should be functions on the result but well
Optional<py::tuple> DuckDBPyConnection::FetchOne() {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchOne();
}

py::list DuckDBPyConnection::FetchMany(idx_t size) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchMany(size);
}

py::list DuckDBPyConnection::FetchAll() {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchAll();
}

py::dict DuckDBPyConnection::FetchNumpy() {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchNumpyInternal();
}

PandasDataFrame DuckDBPyConnection::FetchDF(bool date_as_object) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchDF(date_as_object);
}

PandasDataFrame DuckDBPyConnection::FetchDFChunk(const idx_t vectors_per_chunk, bool date_as_object) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchDFChunk(vectors_per_chunk, date_as_object);
}

duckdb::pyarrow::Table DuckDBPyConnection::FetchArrow(idx_t rows_per_batch) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.ToArrowTable(rows_per_batch);
}

py::dict DuckDBPyConnection::FetchPyTorch() {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchPyTorch();
}

py::dict DuckDBPyConnection::FetchTF() {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchTF();
}

PolarsDataFrame DuckDBPyConnection::FetchPolars(idx_t rows_per_batch, bool lazy) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.ToPolars(rows_per_batch, lazy);
}

duckdb::pyarrow::RecordBatchReader DuckDBPyConnection::FetchRecordBatchReader(const idx_t rows_per_batch) {
	if (!con.HasResult()) {
		throw InvalidInputException("No open result set");
	}
	auto &result = con.GetResult();
	return result.FetchRecordBatchReader(rows_per_batch);
}

case_insensitive_map_t<Value> TransformPyConfigDict(const py::dict &py_config_dict) {
	case_insensitive_map_t<Value> config_dict;
	for (auto &kv : py_config_dict) {
		auto key = py::str(kv.first);
		auto val = py::str(kv.second);
		config_dict[key] = Value(val);
	}
	return config_dict;
}

static bool HasJupyterProgressBarDependencies() {
	auto &import_cache = *DuckDBPyConnection::ImportCache();
	if (!import_cache.ipywidgets()) {
		// ipywidgets not installed, needed to support the progress bar
		return false;
	}
	return true;
}

static void SetDefaultConfigArguments(ClientContext &context) {
	if (!DuckDBPyConnection::IsInteractive()) {
		// Don't need to set any special default arguments
		return;
	}

	auto &config = ClientConfig::GetConfig(context);
	config.enable_progress_bar = true;

	if (!DuckDBPyConnection::IsJupyter()) {
		return;
	}
	if (!HasJupyterProgressBarDependencies()) {
		// Disable progress bar altogether
		config.system_progress_bar_disable_reason =
		    "required package 'ipywidgets' is missing, which is needed to render progress bars in Jupyter";
		config.enable_progress_bar = false;
		return;
	}

	// Set the function used to create the display for the progress bar
	context.config.display_create_func = JupyterProgressBarDisplay::Create;
}

void InstantiateNewInstance(DuckDB &db) {
	auto &db_instance = *db.instance;
	PandasScanFunction scan_fun;
	MapFunction map_fun;

	TableFunctionSet map_set(map_fun.name);
	map_set.AddFunction(std::move(map_fun));
	CreateTableFunctionInfo map_info(std::move(map_set));
	map_info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;

	TableFunctionSet scan_set(scan_fun.name);
	scan_set.AddFunction(std::move(scan_fun));
	CreateTableFunctionInfo scan_info(std::move(scan_set));
	scan_info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;

	auto &system_catalog = Catalog::GetSystemCatalog(db_instance);
	auto transaction = CatalogTransaction::GetSystemTransaction(db_instance);

	system_catalog.CreateFunction(transaction, map_info);
	system_catalog.CreateFunction(transaction, scan_info);
}

static shared_ptr<DuckDBPyConnection> FetchOrCreateInstance(const string &database_path, DBConfig &config) {
	auto res = make_shared_ptr<DuckDBPyConnection>();
	bool cache_instance = database_path != ":memory:" && !database_path.empty();
	config.replacement_scans.emplace_back(PythonReplacementScan::Replace);
	{
		D_ASSERT(py::gil_check());
		py::gil_scoped_release release;
		unique_lock<mutex> lock(res->py_connection_lock);
		auto database =
		    instance_cache.GetOrCreateInstance(database_path, config, cache_instance, InstantiateNewInstance);
		res->con.SetDatabase(std::move(database));
		res->con.SetConnection(make_uniq<Connection>(res->con.GetDatabase()));
	}
	return res;
}

bool IsDefaultConnectionString(const string &database, bool read_only, case_insensitive_map_t<Value> &config) {
	bool is_default = StringUtil::CIEquals(database, ":default:");
	if (!is_default) {
		return false;
	}
	// Only allow fetching the default connection when no options are passed
	if (read_only == true || !config.empty()) {
		throw InvalidInputException("Default connection fetching is only allowed without additional options");
	}
	return true;
}

static string GetPathString(const py::object &path) {
	auto &import_cache = *DuckDBPyConnection::ImportCache();
	const bool is_path = py::isinstance(path, import_cache.pathlib.Path());
	if (is_path || py::isinstance<py::str>(path)) {
		return std::string(py::str(path));
	}
	string actual_type = py::str(path.get_type());
	throw InvalidInputException("Please provide either a str or a pathlib.Path, not %s", actual_type);
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Connect(const py::object &database_p, bool read_only,
                                                           const py::dict &config_options) {
	auto config_dict = TransformPyConfigDict(config_options);
	auto database = GetPathString(database_p);
	if (IsDefaultConnectionString(database, read_only, config_dict)) {
		return DuckDBPyConnection::DefaultConnection();
	}

	DBConfig config(read_only);
	config.AddExtensionOption("pandas_analyze_sample",
	                          "The maximum number of rows to sample when analyzing a pandas object column.",
	                          LogicalType::UBIGINT, Value::UBIGINT(1000));
	config.AddExtensionOption("python_enable_replacements",
	                          "Whether variables visible to the current stack should be used for replacement scans.",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption(
	    "python_scan_all_frames",
	    "If set, restores the old behavior of scanning all preceding frames to locate the referenced variable.",
	    LogicalType::BOOLEAN, Value::BOOLEAN(false));
	if (!DuckDBPyConnection::IsJupyter()) {
		config_dict["duckdb_api"] = Value("python/" + DuckDBPyConnection::FormattedPythonVersion());
	} else {
		config_dict["duckdb_api"] = Value("python/" + DuckDBPyConnection::FormattedPythonVersion() + " jupyter");
	}
	config.SetOptionsByName(config_dict);

	auto res = FetchOrCreateInstance(database, config);
	auto &client_context = *res->con.GetConnection().context;
	SetDefaultConfigArguments(client_context);
	return res;
}

vector<Value> DuckDBPyConnection::TransformPythonParamList(const py::handle &params) {
	vector<Value> args;
	args.reserve(py::len(params));

	for (auto param : params) {
		args.emplace_back(TransformPythonValue(param, LogicalType::UNKNOWN, false));
	}
	return args;
}

case_insensitive_map_t<BoundParameterData> DuckDBPyConnection::TransformPythonParamDict(const py::dict &params) {
	case_insensitive_map_t<BoundParameterData> args;

	for (auto pair : params) {
		auto &key = pair.first;
		auto &value = pair.second;
		args[std::string(py::str(key))] = BoundParameterData(TransformPythonValue(value, LogicalType::UNKNOWN, false));
	}
	return args;
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::DefaultConnection() {
	return default_connection.Get();
}

void DuckDBPyConnection::SetDefaultConnection(shared_ptr<DuckDBPyConnection> connection) {
	return default_connection.Set(std::move(connection));
}

PythonImportCache *DuckDBPyConnection::ImportCache() {
	if (!import_cache) {
		import_cache = make_shared_ptr<PythonImportCache>();
	}
	return import_cache.get();
}

ModifiedMemoryFileSystem &DuckDBPyConnection::GetObjectFileSystem() {
	if (!internal_object_filesystem) {
		D_ASSERT(!FileSystemIsRegistered("DUCKDB_INTERNAL_OBJECTSTORE"));
		auto &import_cache_py = *ImportCache();
		auto modified_memory_fs = import_cache_py.duckdb.filesystem.ModifiedMemoryFileSystem();
		if (modified_memory_fs.ptr() == nullptr) {
			throw InvalidInputException(
			    "This operation could not be completed because required module 'fsspec' is not installed");
		}
		internal_object_filesystem = make_shared_ptr<ModifiedMemoryFileSystem>(modified_memory_fs());
		auto &abstract_fs = reinterpret_cast<AbstractFileSystem &>(*internal_object_filesystem);
		RegisterFilesystem(abstract_fs);
	}
	return *internal_object_filesystem;
}

bool DuckDBPyConnection::IsInteractive() {
	return DuckDBPyConnection::environment != PythonEnvironmentType::NORMAL;
}

shared_ptr<DuckDBPyConnection> DuckDBPyConnection::Enter() {
	return shared_from_this();
}

void DuckDBPyConnection::Exit(DuckDBPyConnection &self, const py::object &exc_type, const py::object &exc,
                              const py::object &traceback) {
	self.Close();
	if (exc_type.ptr() != Py_None) {
		// Propagate the exception if any occurred
		PyErr_SetObject(exc_type.ptr(), exc.ptr());
		throw py::error_already_set();
	}
}

void DuckDBPyConnection::Cleanup() {
	default_connection.Set(nullptr);
	import_cache.reset();
}

bool DuckDBPyConnection::IsPandasDataframe(const py::object &object) {
	if (!ModuleIsLoaded<PandasCacheItem>()) {
		return false;
	}
	auto &import_cache_py = *DuckDBPyConnection::ImportCache();
	return py::isinstance(object, import_cache_py.pandas.DataFrame());
}

bool IsValidNumpyDimensions(const py::handle &object, int &dim) {
	// check the dimensions of numpy arrays
	// should only be called by IsAcceptedNumpyObject
	auto &import_cache = *DuckDBPyConnection::ImportCache();
	if (!py::isinstance(object, import_cache.numpy.ndarray())) {
		return false;
	}
	auto shape = (py::cast<py::array>(object)).attr("shape");
	if (py::len(shape) != 1) {
		return false;
	}
	int cur_dim = (shape.attr("__getitem__")(0)).cast<int>();
	dim = dim == -1 ? cur_dim : dim;
	return dim == cur_dim;
}
NumpyObjectType DuckDBPyConnection::IsAcceptedNumpyObject(const py::object &object) {
	if (!ModuleIsLoaded<NumpyCacheItem>()) {
		return NumpyObjectType::INVALID;
	}
	auto &import_cache = *DuckDBPyConnection::ImportCache();
	if (py::isinstance(object, import_cache.numpy.ndarray())) {
		auto len = py::len((py::cast<py::array>(object)).attr("shape"));
		switch (len) {
		case 1:
			return NumpyObjectType::NDARRAY1D;
		case 2:
			return NumpyObjectType::NDARRAY2D;
		default:
			return NumpyObjectType::INVALID;
		}
	} else if (py::is_dict_like(object)) {
		int dim = -1;
		for (auto item : py::cast<py::dict>(object)) {
			if (!IsValidNumpyDimensions(item.second, dim)) {
				return NumpyObjectType::INVALID;
			}
		}
		return NumpyObjectType::DICT;
	} else if (py::is_list_like(object)) {
		int dim = -1;
		for (auto item : py::cast<py::list>(object)) {
			if (!IsValidNumpyDimensions(item, dim)) {
				return NumpyObjectType::INVALID;
			}
		}
		return NumpyObjectType::LIST;
	}
	return NumpyObjectType::INVALID;
}

PyArrowObjectType DuckDBPyConnection::GetArrowType(const py::handle &obj) {
	D_ASSERT(py::gil_check());

	if (py::isinstance<py::capsule>(obj)) {
		auto capsule = py::reinterpret_borrow<py::capsule>(obj);
		if (string(capsule.name()) != "arrow_array_stream") {
			throw InvalidInputException("Expected a 'arrow_array_stream' PyCapsule, got: %s", string(capsule.name()));
		}
		auto stream = capsule.get_pointer<struct ArrowArrayStream>();
		if (!stream->release) {
			throw InvalidInputException("The ArrowArrayStream was already released");
		}
		return PyArrowObjectType::PyCapsule;
	}

	if (ModuleIsLoaded<PyarrowCacheItem>()) {
		auto &import_cache = *DuckDBPyConnection::ImportCache();
		// First Verify Lib Types
		auto table_class = import_cache.pyarrow.Table();
		auto record_batch_reader_class = import_cache.pyarrow.RecordBatchReader();
		auto message_reader_class = import_cache.pyarrow.ipc.MessageReader();
		if (py::isinstance(obj, table_class)) {
			return PyArrowObjectType::Table;
		} else if (py::isinstance(obj, record_batch_reader_class)) {
			return PyArrowObjectType::RecordBatchReader;
		} else if (py::isinstance(obj, message_reader_class)) {
			return PyArrowObjectType::MessageReader;
		}

		if (ModuleIsLoaded<PyarrowDatasetCacheItem>()) {
			// Then Verify dataset types
			auto dataset_class = import_cache.pyarrow.dataset.Dataset();
			auto scanner_class = import_cache.pyarrow.dataset.Scanner();

			if (py::isinstance(obj, scanner_class)) {
				return PyArrowObjectType::Scanner;
			} else if (py::isinstance(obj, dataset_class)) {
				return PyArrowObjectType::Dataset;
			}
		}
	}

	if (py::hasattr(obj, "__arrow_c_stream__")) {
		return PyArrowObjectType::PyCapsuleInterface;
	}

	return PyArrowObjectType::Invalid;
}

bool DuckDBPyConnection::IsAcceptedArrowObject(const py::object &object) {
	return DuckDBPyConnection::GetArrowType(object) != PyArrowObjectType::Invalid;
}

} // namespace duckdb
