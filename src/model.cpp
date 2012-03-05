#include <sstream>
#include <fstream>
#include <cstdlib>
#include <iterator>
#include <limits>
#include "model.h"

using namespace std;

const char *MODEL_DIR = "models";
const char *PREDICTION_DIR = "predictions";

void slice(const rvec &source, rvec &target, const vector<int> &indexes) {
	target.resize(indexes.size());
	for (int i = 0; i < indexes.size(); ++i) {
		target(i) = source(indexes[i]);
	}
}

model::model(const std::string &name, const std::string &type) 
: name(name), type(type)
{
	stringstream ss;
	ss << MODEL_DIR << "/" << name << "." << type;
	path = ss.str();
	
	if (!get_option("log_predictions").empty()) {
		ss.str("");
		ss << PREDICTION_DIR << "/" << name << "." << type;
		string p = ss.str();
		predlog.open(p.c_str(), ios_base::app);
	}
}

void model::finish() {
	if (!get_option("save_models").empty()) {
		ofstream os(path.c_str());
		if (os.is_open()) {
			save(os);
			os.close();
		}
	}
}

void model::init() {
	ifstream is(path.c_str());
	if (is.is_open()) {
		load(is);
	}
}

float model::test(const rvec &x, const rvec &y) {
	rvec py(y.size());
	float error;
	if (!predict(x, py)) {
		error = numeric_limits<double>::signaling_NaN();
	} else {
		error = (py - y).norm();
	}
	
	if (predlog.is_open()) {
		predlog << x << " ; " << y << " ; " << py << " ; " << error << endl;
	}
	return error;
}

multi_model::multi_model() {
}

multi_model::~multi_model() {
	list<model_config*>::iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		delete *i;
	}
}

bool multi_model::predict(const rvec &x, rvec &y) {
	list<model_config*>::const_iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		model_config *cfg = *i;
		DATAVIS("BEGIN '" << cfg->name << "'" << endl)
		rvec yp(cfg->ally ? y.size() : cfg->yinds.size());
		bool success;
		if (cfg->allx) {
			success = cfg->mdl->predict(x, yp);
		} else {
			rvec x1;
			slice(x, x1,  cfg->xinds);
			success = cfg->mdl->predict(x1, yp);
		}
		if (!success) {
			return false;
		}
		if (cfg->ally) {
			y = yp;
		} else {
			for (int j = 0; j < cfg->yinds.size(); ++j) {
				y[cfg->yinds[j]] = yp[j];
			}
		}
		DATAVIS("END" << endl)
	}
	return true;
}

void multi_model::learn(const rvec &x, const rvec &y, float dt) {
	list<model_config*>::iterator i;
	int j;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		model_config *cfg = *i;
		rvec xp, yp;
		if (cfg->allx) {
			xp = x;
		} else {
			slice(x, xp, cfg->xinds);
		}
		if (cfg->ally) {
			yp = y;
		} else {
			slice(y, yp, cfg->yinds);
		}
		DATAVIS("BEGIN '" << cfg->name << "'" << endl)
		cfg->mdl->learn(xp, yp, dt);
		DATAVIS("END" << endl)
	}
}

float multi_model::test(const rvec &x, const rvec &y) {
	float s = 0.0;
	list<model_config*>::iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		model_config *cfg = *i;
		DATAVIS("BEGIN '" << cfg->name << "'" << endl)
		rvec xp, yp;
		if (cfg->allx) {
			xp = x;
		} else {
			slice(x, xp, cfg->xinds);
		}
		if (cfg->ally) {
			yp = y;
		} else {
			slice(y, yp, cfg->yinds);
		}
		float d = cfg->mdl->test(xp, yp);
		if (d < 0.) {
			DATAVIS("'pred error' 'no prediction'" << endl)
			s = -1.;
		} else if (s >= 0.) {
			DATAVIS("'pred error' " << d << endl)
			s += d;
		}
		DATAVIS("END" << endl)
	}
	if (s >= 0.) {
		return s / active_models.size();
	}
	return -1.;
}

string multi_model::assign_model
( const string &name, 
  const vector<string> &inputs, bool all_inputs,
  const vector<string> &outputs, bool all_outputs )
{
	model *m;
	model_config *cfg;
	if (!map_get(model_db, name, m)) {
		return "no model";
	}
	
	cfg = new model_config();
	cfg->name = name;
	cfg->mdl = m;
	cfg->allx = all_inputs;
	cfg->ally = all_outputs;
	
	if (all_inputs) {
		if (m->get_input_size() >= 0 && m->get_input_size() != prop_vec.size()) {
			return "size mismatch";
		}
	} else {
		if (m->get_input_size() >= 0 && m->get_input_size() != inputs.size()) {
			return "size mismatch";
		}
		cfg->xprops = inputs;
		if (!find_indexes(inputs, cfg->xinds)) {
			delete cfg;
			return "property not found";
		}
	}
	if (all_outputs) {
		if (m->get_output_size() >= 0 && m->get_output_size() != prop_vec.size()) {
			return "size mismatch";
		}
	} else {
		if (m->get_output_size() >= 0 && m->get_output_size() != outputs.size()) {
			return "size mismatch";
		}
		cfg->yprops = outputs;
		if (!find_indexes(outputs, cfg->yinds)) {
			delete cfg;
			return "property not found";
		}
	}
	active_models.push_back(cfg);
	return "";
}

void multi_model::unassign_model(const string &name) {
	list<model_config*>::iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		if ((**i).name == name) {
			active_models.erase(i);
			return;
		}
	}
}

bool multi_model::find_indexes(const vector<string> &props, vector<int> &indexes) {
	vector<string>::const_iterator i;

	for (i = props.begin(); i != props.end(); ++i) {
		int index = find(prop_vec.begin(), prop_vec.end(), *i) - prop_vec.begin();
		if (index == prop_vec.size()) {
			cerr << "PROPERTY NOT FOUND " << *i << endl;
			return false;
		}
		indexes.push_back(index);
	}
	return true;
}

bool multi_model::cli_inspect(int first_arg, const vector<string> &args, string &out) const {
	stringstream ss;
	map<string, model*>::const_iterator i;
	if (first_arg >= args.size()) {
		ss << "Current models:" << endl;
		for (i = model_db.begin(); i != model_db.end(); ++i) {
			ss << i->first << endl;
		}
		out = ss.str();
		return true;
	}
	i = model_db.find(args[first_arg]);
	if (i == model_db.end()) {
		out = "no such model";
		return false;
	}
	return i->second->cli_inspect(first_arg + 1, args, out);
}

