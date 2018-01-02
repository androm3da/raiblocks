#include <rai/qt/qt.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <sstream>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>

#include "clipboardproxy.hpp"

namespace
{
void show_line_error (QLineEdit & line)
{
	line.setStyleSheet ("QLineEdit { color: red }");
}
void show_line_ok (QLineEdit & line)
{
	line.setStyleSheet ("QLineEdit { color: black }");
}
void show_line_success (QLineEdit & line)
{
	line.setStyleSheet ("QLineEdit { color: blue }");
}
void show_label_error (QLabel & label)
{
	label.setStyleSheet ("QLabel { color: red }");
}
void show_label_ok (QLabel & label)
{
	label.setStyleSheet ("QLabel { color: black }");
}
void show_button_error (QPushButton & button)
{
	button.setStyleSheet ("QPushButton { color: red }");
}
void show_button_ok (QPushButton & button)
{
	button.setStyleSheet ("QPushButton { color: black }");
}
void show_button_success (QPushButton & button)
{
	button.setStyleSheet ("QPushButton { color: blue }");
}
}

bool rai_qt::eventloop_processor::event (QEvent * event_a)
{
	assert (dynamic_cast<rai_qt::eventloop_event *> (event_a) != nullptr);
	static_cast<rai_qt::eventloop_event *> (event_a)->action ();
	return true;
}

rai_qt::eventloop_event::eventloop_event (std::function<void()> const & action_a) :
QEvent (QEvent::Type::User),
action (action_a)
{
}

rai_qt::self_pane::self_pane (rai_qt::wallet & wallet_a, rai::account const & account_a) :
wallet (wallet_a)
{
}

void rai_qt::self_pane::refresh_balance ()
{
	auto balance (wallet.node.balance_pending (wallet.account));

	auto strBalance = wallet.format_balance (balance.first);
	auto strPending = !balance.second.is_zero ()
	? wallet.format_balance (balance.second)
	: "";
	this->setBalance (QString::fromStdString (strBalance));
	this->setPending (QString::fromStdString (strPending));
}

QString rai_qt::self_pane::getAccount ()
{
	return m_account;
}

void rai_qt::self_pane::setAccount (QString account)
{
	if (account != m_account)
	{
		m_account = account;
		Q_EMIT accountChanged (account);
	}
}

QString rai_qt::self_pane::getBalance ()
{
	return m_balance;
}

void rai_qt::self_pane::setBalance (QString balance)
{
	if (balance != m_balance)
	{
		m_balance = balance;
		Q_EMIT balanceChanged (balance);
	}
}

QString rai_qt::self_pane::getPending ()
{
	return m_pending;
}

void rai_qt::self_pane::setPending (QString pending)
{
	if (pending != m_pending)
	{
		m_pending = pending;
		Q_EMIT pendingChanged (pending);
	}
}

rai_qt::account_item::account_item (QString balance, QString account, bool isAdhoc, QObject * parent) :
m_balance (balance),
m_account (account),
m_isAdhoc (isAdhoc){
	Q_UNUSED (parent)
}

QString rai_qt::account_item::getAccount ()
{
	return m_account;
}

QString rai_qt::account_item::getBalance ()
{
	return m_balance;
}

bool rai_qt::account_item::isAdhoc ()
{
	return m_isAdhoc;
}

rai_qt::accounts::accounts (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
import_wallet (new QPushButton ("Import wallet")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (import_wallet);
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::clicked, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (import_wallet, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (this->wallet.import.window);
	});
	refresh_wallet_balance ();
}

void rai_qt::accounts::refresh_wallet_balance ()
{
	rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, false);
	rai::uint128_t balance (0);
	rai::uint128_t pending (0);
	for (auto i (this->wallet.wallet_m->store.begin (transaction)), j (this->wallet.wallet_m->store.end ()); i != j; ++i)
	{
		rai::public_key key (i->first.uint256 ());
		balance = balance + (this->wallet.node.ledger.account_balance (transaction, key));
		pending = pending + (this->wallet.node.ledger.account_pending (transaction, key));
	}
	auto strBalance = wallet.format_balance (balance);
	auto strPending = pending.is_zero () ? "" : wallet.format_balance (pending);

	setTotalBalance (QString::fromStdString (strBalance));
	setTotalPending (QString::fromStdString (strPending));

	this->wallet.node.alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (60), [this]() {
		this->wallet.application.postEvent (&this->wallet.processor, new eventloop_event ([this]() {
			refresh_wallet_balance ();
		}));
	});
}

void rai_qt::accounts::refresh ()
{
	m_model.clear ();
	rai::transaction transaction (wallet.wallet_m->store.environment, nullptr, false);
	for (auto i (wallet.wallet_m->store.begin (transaction)), j (wallet.wallet_m->store.end ()); i != j; ++i)
	{
		rai::public_key key (i->first.uint256 ());
		auto balance_amount (wallet.node.ledger.account_balance (transaction, key));
		bool isAdhoc (wallet.wallet_m->store.key_type (i->second) == rai::key_type::adhoc);
		if (!isAdhoc || (isAdhoc && !balance_amount.is_zero ()))
		{
			std::string balance = wallet.format_balance (balance_amount);
			m_model.append (new account_item (
			QString::fromStdString (balance),
			QString::fromStdString (key.to_account ()), isAdhoc));
		}
	}
	Q_EMIT modelChanged (m_model);
}

void rai_qt::accounts::backupSeed ()
{
	rai::raw_key seed;
	rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, false);
	if (this->wallet.wallet_m->store.valid_password (transaction))
	{
		this->wallet.wallet_m->store.seed (seed, transaction);
		this->wallet.application.clipboard ()->setText (QString (seed.data.to_string ().c_str ()));
		Q_EMIT backupSeedSuccess ();
	}
	else
	{
		Q_EMIT backupSeedFailure ("Wallet is locked, unlock it to enable the backup");
	}
}

void rai_qt::accounts::createAccount ()
{
	rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
	if (this->wallet.wallet_m->store.valid_password (transaction))
	{
		this->wallet.wallet_m->deterministic_insert (transaction);
		refresh ();
		Q_EMIT createAccountSuccess ();
	}
	else
	{
		Q_EMIT createAccountFailure ("Wallet is locked, unlock it to create account");
	}
}

void rai_qt::accounts::insertAdhocKey (QString key_text_wide)
{
	std::string key_text (key_text_wide.toLocal8Bit ());
	rai::raw_key key;
	if (!key.data.decode_hex (key_text))
	{
		Q_EMIT insertAdhocKeyFinished (true);
		this->wallet.wallet_m->insert_adhoc (key);
		this->wallet.accounts.refresh ();
		this->wallet.history.refresh ();
	}
	else
	{
		Q_EMIT insertAdhocKeyFinished (false);
	}
}

void rai_qt::accounts::useAccount (QString account)
{
	auto error (this->wallet.account.decode_account (account.toStdString ()));
	assert (!error);
	this->wallet.refresh ();
}

QList<QObject *> rai_qt::accounts::getModel ()
{
	return m_model;
}

QString rai_qt::accounts::getTotalBalance ()
{
	return m_totalBalance;
}

QString rai_qt::accounts::getTotalPending ()
{
	return m_totalPending;
}

void rai_qt::accounts::setTotalBalance (QString totalBalance)
{
	if (m_totalBalance != totalBalance)
	{
		m_totalBalance = totalBalance;
		Q_EMIT totalBalanceChanged (totalBalance);
	}
}

void rai_qt::accounts::setTotalPending (QString totalPending)
{
	if (m_totalPending != totalPending)
	{
		m_totalPending = totalPending;
		Q_EMIT totalPendingChanged (totalPending);
	}
}

rai_qt::import::import (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
seed_label (new QLabel ("Seed:")),
seed (new QLineEdit),
clear_label (new QLabel ("Modifying seed clears existing keys\nType 'clear keys' below to confirm:")),
clear_line (new QLineEdit),
import_seed (new QPushButton ("Import seed")),
separator (new QFrame),
filename_label (new QLabel ("Path to file:")),
filename (new QLineEdit),
password_label (new QLabel ("Password:")),
password (new QLineEdit),
perform (new QPushButton ("Import")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (seed_label);
	layout->addWidget (seed);
	layout->addWidget (clear_label);
	layout->addWidget (clear_line);
	clear_line->setPlaceholderText ("clear keys");
	layout->addWidget (import_seed);
	layout->addWidget (separator);
	layout->addWidget (filename_label);
	layout->addWidget (filename);
	layout->addWidget (password_label);
	layout->addWidget (password);
	layout->addWidget (perform);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (perform, &QPushButton::released, [this]() {
		std::ifstream stream;
		stream.open (filename->text ().toStdString ().c_str ());
		if (!stream.fail ())
		{
			show_line_ok (*filename);
			std::stringstream contents;
			contents << stream.rdbuf ();
			if (!this->wallet.wallet_m->import (contents.str (), password->text ().toStdString ().c_str ()))
			{
				show_line_ok (*password);
				this->wallet.accounts.refresh ();
				password->clear ();
				filename->clear ();
			}
			else
			{
				show_line_error (*password);
			}
		}
		else
		{
			show_line_error (*filename);
		}
	});
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (import_seed, &QPushButton::released, [this]() {
		if (clear_line->text ().toStdString () == "clear keys")
		{
			show_line_ok (*clear_line);
			rai::raw_key seed_l;
			if (!seed_l.data.decode_hex (seed->text ().toStdString ()))
			{
				bool successful (false);
				{
					rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
					if (this->wallet.wallet_m->store.valid_password (transaction))
					{
						this->wallet.wallet_m->store.seed_set (transaction, seed_l);
						successful = true;
					}
					else
					{
						show_line_error (*seed);
						show_button_error (*import_seed);
						import_seed->setText ("Wallet is locked, unlock it to enable the import");
						QTimer::singleShot (std::chrono::milliseconds (10000).count (), [this]() {
							show_line_ok (*seed);
							show_button_ok (*import_seed);
							import_seed->setText ("Import seed");
						});
					}
				}
				if (successful)
				{
					rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
					this->wallet.account = this->wallet.wallet_m->deterministic_insert (transaction);
					auto count (0);
					for (uint32_t i (1), n (32); i < n; ++i)
					{
						rai::raw_key prv;
						this->wallet.wallet_m->store.deterministic_key (prv, transaction, i);
						rai::keypair pair (prv.data.to_string ());
						auto latest (this->wallet.node.ledger.latest (transaction, pair.pub));
						if (!latest.is_zero ())
						{
							count = i;
							n = i + 32;
						}
					}
					for (uint32_t i (0); i < count; ++i)
					{
						this->wallet.account = this->wallet.wallet_m->deterministic_insert (transaction);
					}
				}
				if (successful)
				{
					seed->clear ();
					clear_line->clear ();
					show_line_ok (*seed);
					show_button_success (*import_seed);
					import_seed->setText ("Successful import of seed");
					this->wallet.refresh ();
					QTimer::singleShot (std::chrono::milliseconds (5000).count (), [this]() {
						show_button_ok (*import_seed);
						import_seed->setText ("Import seed");
					});
				}
			}
			else
			{
				show_line_error (*seed);
				show_button_error (*import_seed);
				if (seed->text ().toStdString ().size () != 64)
				{
					import_seed->setText ("Incorrect seed, length must be 64");
				}
				else
				{
					import_seed->setText ("Incorrect seed. Only HEX characters allowed");
				}
				QTimer::singleShot (std::chrono::milliseconds (5000).count (), [this]() {
					show_button_ok (*import_seed);
					import_seed->setText ("Import seed");
				});
			}
		}
		else
		{
			show_line_error (*clear_line);
			show_button_error (*import_seed);
			import_seed->setText ("Type words 'clear keys'");
			QTimer::singleShot (std::chrono::milliseconds (5000).count (), [this]() {
				show_button_ok (*import_seed);
				import_seed->setText ("Import seed");
			});
		}
	});
}

rai_qt::history_item::history_item (QString type, QString account, QString amount, QString hash, QObject * parent) :
m_type (type),
m_account (account),
m_amount (amount),
m_hash (hash)
{
	Q_UNUSED (parent);
}

QString rai_qt::history_item::getType ()
{
	return m_type;
}

QString rai_qt::history_item::getAccount ()
{
	return m_account;
}

QString rai_qt::history_item::getAmount ()
{
	return m_amount;
}

QString rai_qt::history_item::getHash ()
{
	return m_hash;
}

rai_qt::history::history (rai::ledger & ledger_a, rai::account const & account_a, rai_qt::wallet & wallet_a) :
ledger (ledger_a),
account (account_a),
wallet (wallet_a)
{
}

QList<QObject *> rai_qt::history::getModel ()
{
	return m_model;
}

namespace
{
class short_text_visitor : public rai::block_visitor
{
public:
	short_text_visitor (MDB_txn * transaction_a, rai::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
	{
	}
	void send_block (rai::send_block const & block_a)
	{
		type = "Send";
		account = block_a.hashables.destination;
		amount = ledger.amount (transaction, block_a.hash ());
	}
	void receive_block (rai::receive_block const & block_a)
	{
		type = "Receive";
		account = ledger.account (transaction, block_a.source ());
		amount = ledger.amount (transaction, block_a.source ());
	}
	void open_block (rai::open_block const & block_a)
	{
		type = "Receive";
		if (block_a.hashables.source != rai::genesis_account)
		{
			account = ledger.account (transaction, block_a.hashables.source);
			amount = ledger.amount (transaction, block_a.hash ());
		}
		else
		{
			account = rai::genesis_account;
			amount = rai::genesis_amount;
		}
	}
	void change_block (rai::change_block const & block_a)
	{
		type = "Change";
		amount = 0;
		account = block_a.hashables.representative;
	}
	MDB_txn * transaction;
	rai::ledger & ledger;
	std::string type;
	rai::uint128_t amount;
	rai::account account;
};
}

void rai_qt::history::refresh ()
{
	rai::transaction transaction (ledger.store.environment, nullptr, false);
	m_model.clear ();
	auto hash (ledger.latest (transaction, account));
	short_text_visitor visitor (transaction, ledger);
	while (!hash.is_zero ())
	{
		auto block (ledger.store.block_get (transaction, hash));
		assert (block != nullptr);
		block->visit (visitor);
		auto item = new history_item (
		QString (visitor.type.c_str ()),
		QString (visitor.account.to_account ().c_str ()),
		QString (wallet.format_balance (visitor.amount).c_str ()),
		QString (hash.to_string ().c_str ()));
		hash = block->previous ();
		m_model.append (item);
	}
	Q_EMIT modelChanged (m_model);
}

rai_qt::block_viewer::block_viewer (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
hash_label (new QLabel ("Hash:")),
hash (new QLineEdit),
block_label (new QLabel ("Block:")),
block (new QPlainTextEdit),
successor_label (new QLabel ("Successor:")),
successor (new QLineEdit),
retrieve (new QPushButton ("Retrieve")),
rebroadcast (new QPushButton ("Rebroadcast")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (hash_label);
	layout->addWidget (hash);
	layout->addWidget (block_label);
	layout->addWidget (block);
	layout->addWidget (successor_label);
	layout->addWidget (successor);
	layout->addWidget (retrieve);
	layout->addWidget (rebroadcast);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (retrieve, &QPushButton::released, [this]() {
		rai::block_hash hash_l;
		if (!hash_l.decode_hex (hash->text ().toStdString ()))
		{
			rai::transaction transaction (this->wallet.node.store.environment, nullptr, false);
			auto block_l (this->wallet.node.store.block_get (transaction, hash_l));
			if (block_l != nullptr)
			{
				std::string contents;
				block_l->serialize_json (contents);
				block->setPlainText (contents.c_str ());
				auto successor_l (this->wallet.node.store.block_successor (transaction, hash_l));
				successor->setText (successor_l.to_string ().c_str ());
			}
			else
			{
				block->setPlainText ("Block not found");
			}
		}
		else
		{
			block->setPlainText ("Bad block hash");
		}
	});
	QObject::connect (rebroadcast, &QPushButton::released, [this]() {
		rai::block_hash block;
		auto error (block.decode_hex (hash->text ().toStdString ()));
		if (!error)
		{
			rai::transaction transaction (this->wallet.node.store.environment, nullptr, false);
			if (this->wallet.node.store.block_exists (transaction, block))
			{
				rebroadcast->setEnabled (false);
				this->wallet.node.background ([this, block]() {
					rebroadcast_action (block);
				});
			}
		}
	});
	rebroadcast->setToolTip ("Rebroadcast block into the network");
}

void rai_qt::block_viewer::rebroadcast_action (rai::uint256_union const & hash_a)
{
	auto done (true);
	rai::transaction transaction (wallet.node.ledger.store.environment, nullptr, false);
	auto block (wallet.node.store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		wallet.node.network.republish_block (transaction, std::move (block));
		auto successor (wallet.node.store.block_successor (transaction, hash_a));
		if (!successor.is_zero ())
		{
			done = false;
			QTimer::singleShot (std::chrono::milliseconds (1000).count (), [this, successor]() {
				rebroadcast_action (successor);
			});
		}
	}
	if (done)
	{
		rebroadcast->setEnabled (true);
	}
}

rai_qt::account_viewer::account_viewer (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
account_label (new QLabel ("Account:")),
account_line (new QLineEdit),
refresh (new QPushButton ("Refresh")),
balance_window (new QWidget),
balance_layout (new QHBoxLayout),
balance_label (new QLabel),
history (wallet_a.wallet_m->node.ledger, account, wallet_a),
back (new QPushButton ("Back")),
account (wallet_a.account),
wallet (wallet_a)
{
	layout->addWidget (account_label);
	layout->addWidget (account_line);
	layout->addWidget (refresh);
	balance_layout->addWidget (balance_label);
	balance_layout->addStretch ();
	balance_layout->setContentsMargins (0, 0, 0, 0);
	balance_window->setLayout (balance_layout);
	layout->addWidget (balance_window);
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (refresh, &QPushButton::released, [this]() {
		account.clear ();
		if (!account.decode_account (account_line->text ().toStdString ()))
		{
			show_line_ok (*account_line);
			this->history.refresh ();
			auto balance (this->wallet.node.balance_pending (account));
			auto final_text (std::string ("Balance (XRB): ") + wallet.format_balance (balance.first));
			if (!balance.second.is_zero ())
			{
				final_text += "\nPending: " + wallet.format_balance (balance.second);
			}
			balance_label->setText (QString (final_text.c_str ()));
		}
		else
		{
			show_line_error (*account_line);
			balance_label->clear ();
		}
	});
}

rai_qt::status::status (rai_qt::wallet & wallet_a) :
wallet (wallet_a)
{
	active.insert (rai_qt::status_types::nominal);
	set_text ();
}

void rai_qt::status::erase (rai_qt::status_types status_a)
{
	assert (status_a != rai_qt::status_types::nominal);
	auto erased (active.erase (status_a));
	(void)erased;
	set_text ();
}

void rai_qt::status::insert (rai_qt::status_types status_a)
{
	assert (status_a != rai_qt::status_types::nominal);
	active.insert (status_a);
	set_text ();
}

QColor rai_qt::status::getColor ()
{
	return m_color;
}

QString rai_qt::status::getText ()
{
	return m_text;
}

void rai_qt::status::set_text ()
{
	QString aText = text ();
	QColor aColor = color ();

	if (m_text != aText)
	{
		m_text = aText;
		Q_EMIT textChanged (aText);
	}

	if (m_color != aColor)
	{
		m_color = aColor;
		Q_EMIT colorChanged (aColor);
	}
}

QString rai_qt::status::text ()
{
	assert (!active.empty ());
	std::string result;
	size_t unchecked (0);
	std::string count_string;
	{
		rai::transaction transaction (wallet.wallet_m->node.store.environment, nullptr, false);
		auto size (wallet.wallet_m->node.store.block_count (transaction));
		unchecked = wallet.wallet_m->node.store.unchecked_count (transaction);
		count_string = std::to_string (size.sum ());
	}

	switch (*active.begin ())
	{
		case rai_qt::status_types::disconnected:
			result = "Status: Disconnected";
			break;
		case rai_qt::status_types::working:
			result = "Status: Generating proof of work";
			break;
		case rai_qt::status_types::synchronizing:
			result = "Status: Synchronizing";
			break;
		case rai_qt::status_types::locked:
			result = "Status: Wallet locked";
			break;
		case rai_qt::status_types::vulnerable:
			result = "Status: Wallet password empty";
			break;
		case rai_qt::status_types::active:
			result = "Status: Wallet active";
			break;
		case rai_qt::status_types::nominal:
			result = "Status: Running";
			break;
		default:
			assert (false);
			break;
	}

	result += ", Block: ";
	if (unchecked != 0 && wallet.wallet_m->node.bootstrap_initiator.in_progress ())
	{
		count_string += " (" + std::to_string (unchecked) + ")";
	}
	result += count_string.c_str ();

	return QString::fromStdString (result);
}

QColor rai_qt::status::color ()
{
	assert (!active.empty ());
	std::string result;
	switch (*active.begin ())
	{
		case rai_qt::status_types::disconnected:
			result = "red";
			break;
		case rai_qt::status_types::working:
			result = "blue";
			break;
		case rai_qt::status_types::synchronizing:
			result = "blue";
			break;
		case rai_qt::status_types::locked:
			result = "orange";
			break;
		case rai_qt::status_types::vulnerable:
			result = "blue";
			break;
		case rai_qt::status_types::active:
			result = "black";
			break;
		case rai_qt::status_types::nominal:
			result = "black";
			break;
		default:
			assert (false);
			break;
	}
	return QColor (QString::fromStdString (result));
}

rai::uint128_t RenderingRatio::to_uint128 (RenderingRatio::Type ratio)
{
	switch (ratio)
	{
		case RenderingRatio::Type::XRB:
			return rai::Mxrb_ratio;
		case RenderingRatio::Type::MilliXRB:
			return rai::kxrb_ratio;
		case RenderingRatio::Type::MicroXRB:
			return rai::xrb_ratio;
		default:
			assert (false);
			break;
	}
}

QString RenderingRatio::to_string (RenderingRatio::Type ratio)
{
	switch (ratio)
	{
		case RenderingRatio::Type::XRB:
			return QString ("XRB");
		case RenderingRatio::Type::MilliXRB:
			return QString ("mXRB");
		case RenderingRatio::Type::MicroXRB:
			return QString ("uXRB");
		default:
			assert (false);
			break;
	}
}

rai_qt::wallet::wallet (QApplication & application_a, rai_qt::eventloop_processor & processor_a, rai::node & node_a, std::shared_ptr<rai::wallet> wallet_a, rai::account & account_a) :
rendering_ratio (rai::Mxrb_ratio),
node (node_a),
wallet_m (wallet_a),
account (account_a),
processor (processor_a),
history (node.ledger, account, *this),
accounts (*this),
self (*this, account_a),
settings (*this),
advanced (*this),
block_creation (*this),
block_entry (*this),
block_viewer (*this),
account_viewer (*this),
import (*this),
application (application_a),
main_stack (new QStackedWidget),
client_window (new QWidget),
client_layout (new QVBoxLayout),
entry_window (new QWidget),
entry_window_layout (new QVBoxLayout),
separator (new QFrame),
accounts_button (new QPushButton ("Accounts")),
show_advanced (new QPushButton ("Advanced")),
active_status (*this)
{
	update_connected ();
	empty_password ();
	settings.update_locked (true, true);

	entry_window_layout->addWidget (accounts_button);
	entry_window_layout->addWidget (show_advanced);
	entry_window_layout->setContentsMargins (0, 0, 0, 0);
	entry_window_layout->setSpacing (5);
	entry_window->setLayout (entry_window_layout);

	main_stack->addWidget (entry_window);
	separator->setFrameShape (QFrame::HLine);
	separator->setFrameShadow (QFrame::Sunken);

	client_layout->addWidget (separator);
	client_layout->addWidget (main_stack);
	client_layout->setSpacing (0);
	client_layout->setContentsMargins (0, 0, 0, 0);
	client_window->setLayout (client_layout);
	client_window->resize (320, 480);
	client_window->setStyleSheet ("\
		QLineEdit { padding: 3px; } \
	");
	refresh ();

	application.setAttribute (Qt::AA_EnableHighDpiScaling);

	QQmlEngine * engine = new QQmlEngine;
	engine->rootContext ()->setContextProperty (QString ("RAIBLOCKS_VERSION_MAJOR"), int(RAIBLOCKS_VERSION_MAJOR));
	engine->rootContext ()->setContextProperty (QString ("RAIBLOCKS_VERSION_MINOR"), int(RAIBLOCKS_VERSION_MINOR));
	engine->rootContext ()->setContextProperty (QString ("rai_self_pane"), &self);
	engine->rootContext ()->setContextProperty (QString ("rai_status"), &active_status);
	engine->rootContext ()->setContextProperty (QString ("rai_history"), &history);
	engine->rootContext ()->setContextProperty (QString ("rai_accounts"), &accounts);
	engine->rootContext ()->setContextProperty (QString ("rai_settings"), &settings);
	engine->rootContext ()->setContextProperty (QString ("rai_wallet"), this);

	qmlRegisterType<ClipboardProxy> ("net.raiblocks", 1, 0, "ClipboardProxy");

	qmlRegisterUncreatableType<SendResult> (
	"net.raiblocks", 1, 0, "SendResult", "SendResult is not instantiable");
	qmlRegisterUncreatableType<RenderingRatio> (
	"net.raiblocks", 1, 0, "RenderingRatio", "RenderingRatio is not instantiable");

	QQmlComponent component (engine, QUrl (QStringLiteral ("qrc:/gui/main.qml")));
	m_qmlgui = std::unique_ptr<QObject> (component.create ());
}

void rai_qt::wallet::start ()
{
	std::weak_ptr<rai_qt::wallet> this_w (shared_from_this ());
	QObject::connect (accounts_button, &QPushButton::released, [this_w]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->push_main_stack (this_l->accounts.window);
		}
	});
	QObject::connect (show_advanced, &QPushButton::released, [this_w]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->push_main_stack (this_l->advanced.window);
		}
	});
	QObject::connect (this, &rai_qt::wallet::sendFinished, [=]() {
		this->setProcessingSend (false);
	});
	node.observers.blocks.add ([this_w](std::shared_ptr<rai::block> block_a, rai::account const & account_a, rai::amount const &) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, block_a, account_a]() {
				if (auto this_l = this_w.lock ())
				{
					if (this_l->wallet_m->exists (account_a))
					{
						this_l->accounts.refresh ();
					}
					if (account_a == this_l->account)
					{
						this_l->history.refresh ();
					}
				}
			}));
		}
	});
	node.observers.account_balance.add ([this_w](rai::account const & account_a, bool is_pending) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, account_a]() {
				if (auto this_l = this_w.lock ())
				{
					if (account_a == this_l->account)
					{
						this_l->self.refresh_balance ();
					}
				}
			}));
		}
	});
	node.observers.wallet.add ([this_w](bool active_a) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, active_a]() {
				if (auto this_l = this_w.lock ())
				{
					if (active_a)
					{
						this_l->active_status.insert (rai_qt::status_types::active);
					}
					else
					{
						this_l->active_status.erase (rai_qt::status_types::active);
					}
				}
			}));
		}
	});
	node.observers.endpoint.add ([this_w](rai::endpoint const &) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w]() {
				if (auto this_l = this_w.lock ())
				{
					this_l->update_connected ();
				}
			}));
		}
	});
	node.observers.disconnect.add ([this_w]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w]() {
				if (auto this_l = this_w.lock ())
				{
					this_l->update_connected ();
				}
			}));
		}
	});
	node.bootstrap_initiator.add_observer ([this_w](bool active_a) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, active_a]() {
				if (auto this_l = this_w.lock ())
				{
					if (active_a)
					{
						this_l->active_status.insert (rai_qt::status_types::synchronizing);
					}
					else
					{
						this_l->active_status.erase (rai_qt::status_types::synchronizing);
					}
				}
			}));
		}
	});
	node.work.work_observers.add ([this_w](bool working) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, working]() {
				if (auto this_l = this_w.lock ())
				{
					if (working)
					{
						this_l->active_status.insert (rai_qt::status_types::working);
					}
					else
					{
						this_l->active_status.erase (rai_qt::status_types::working);
					}
				}
			}));
		}
	});
	wallet_m->lock_observer = [this_w](bool invalid, bool vulnerable) {
		if (auto this_l = this_w.lock ())
		{
			this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, invalid, vulnerable]() {
				if (auto this_l = this_w.lock ())
				{
					this_l->settings.update_locked (invalid, vulnerable);
				}
			}));
		}
	};
}

void rai_qt::wallet::refresh ()
{
	{
		rai::transaction transaction (wallet_m->store.environment, nullptr, false);
		assert (wallet_m->store.exists (transaction, account));
	}
	self.setAccount (QString (account.to_account ().c_str ()));
	self.refresh_balance ();
	accounts.refresh ();
	accounts.refresh_wallet_balance ();
	history.refresh ();
	account_viewer.history.refresh ();
	settings.refresh_representative ();
}

void rai_qt::wallet::update_connected ()
{
	if (node.peers.empty ())
	{
		active_status.insert (rai_qt::status_types::disconnected);
	}
	else
	{
		active_status.erase (rai_qt::status_types::disconnected);
	}
}

void rai_qt::wallet::empty_password ()
{
	this->node.alarm.add (std::chrono::system_clock::now () + std::chrono::seconds (3), [this]() {
		wallet_m->enter_password (std::string (""));
	});
}

void rai_qt::wallet::setRenderingRatio (RenderingRatio::Type renderingRatio)
{
	if (m_renderingRatio != renderingRatio)
	{
		this->rendering_ratio = RenderingRatio::to_uint128 (renderingRatio);
		m_renderingRatio = renderingRatio;
		Q_EMIT renderingRatioChanged (renderingRatio);
		application.postEvent (&processor, new eventloop_event ([this]() {
			this->refresh ();
		}));
	}
}

RenderingRatio::Type rai_qt::wallet::getRenderingRatio ()
{
	return m_renderingRatio;
}

std::string rai_qt::wallet::format_balance (rai::uint128_t const & balance) const
{
	auto balance_str = rai::amount (balance).format_balance (rendering_ratio, 2, true, std::locale (""));
	auto unit = std::string ("XRB");
	if (rendering_ratio == rai::kxrb_ratio)
	{
		unit = std::string ("mXRB");
	}
	else if (rendering_ratio == rai::xrb_ratio)
	{
		unit = std::string ("uXRB");
	}
	return balance_str + " " + unit;
}

void rai_qt::wallet::push_main_stack (QWidget * widget_a)
{
	main_stack->addWidget (widget_a);
	main_stack->setCurrentIndex (main_stack->count () - 1);
}

void rai_qt::wallet::pop_main_stack ()
{
	main_stack->removeWidget (main_stack->currentWidget ());
}

void rai_qt::wallet::send (QString amount_text, QString account_text)
{
	this->setProcessingSend (true);

	rai::amount amount;
	if (amount.decode_dec (amount_text.toStdString ()))
	{
		Q_EMIT sendFinished (SendResult::Type::BadAmountNumber);
		return;
	}

	rai::uint128_t actual (amount.number () * this->rendering_ratio);
	if (actual / this->rendering_ratio != amount.number ())
	{
		Q_EMIT sendFinished (SendResult::Type::AmountTooBig);
		return;
	}

	std::string account_text_narrow (account_text.toLocal8Bit ());
	rai::account account_l;
	auto parse_error (account_l.decode_account (account_text_narrow));
	if (parse_error)
	{
		Q_EMIT sendFinished (SendResult::Type::BadDestinationAccount);
		return;
	}

	auto balance (this->node.balance (this->account));
	if (actual > balance)
	{
		Q_EMIT sendFinished (SendResult::Type::NotEnoughBalance);
		return;
	}

	rai::transaction transaction (this->wallet_m->store.environment, nullptr, false);
	if (!this->wallet_m->store.valid_password (transaction))
	{
		Q_EMIT sendFinished (SendResult::Type::WalletIsLocked);
		return;
	}

	std::weak_ptr<rai_qt::wallet> this_w (shared_from_this ());
	this->node.background ([this_w, account_l, actual]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->wallet_m->send_async (this_l->account, account_l, actual, [this_w](std::shared_ptr<rai::block> block_a) {
				if (auto this_l = this_w.lock ())
				{
					auto succeeded (block_a != nullptr);
					this_l->application.postEvent (&this_l->processor, new eventloop_event ([this_w, succeeded]() {
						if (auto this_l = this_w.lock ())
						{
							this_l->accounts.refresh ();
							Q_EMIT this_l->sendFinished (
							succeeded ? SendResult::Type::Success
							          : SendResult::Type::BlockSendFailed);
						}
					}));
				}
			});
		}
	});
}

void rai_qt::wallet::setProcessingSend (bool processingSend)
{
	if (m_processingSend != processingSend)
	{
		m_processingSend = processingSend;
		Q_EMIT processingSendChanged (processingSend);
	}
}

bool rai_qt::wallet::isProcessingSend ()
{
	return m_processingSend;
}

rai_qt::settings::settings (rai_qt::wallet & wallet_a) :
wallet (wallet_a)
{
	refresh_representative ();
}

void rai_qt::settings::refresh_representative ()
{
	QString representative;
	rai::transaction transaction (this->wallet.wallet_m->node.store.environment, nullptr, false);
	rai::account_info info;
	auto error (this->wallet.wallet_m->node.store.account_get (transaction, this->wallet.account, info));
	if (!error)
	{
		auto block (this->wallet.wallet_m->node.store.block_get (transaction, info.rep_block));
		assert (block != nullptr);
		representative = QString (block->representative ().to_account ().c_str ());
	}
	else
	{
		representative = QString (this->wallet.wallet_m->store.representative (transaction).to_account ().c_str ());
	}

	if (m_representative != representative)
	{
		m_representative = representative;
		Q_EMIT representativeChanged (representative);
	}
}

void rai_qt::settings::update_locked (bool invalid, bool vulnerable)
{
	if (invalid)
	{
		this->wallet.active_status.insert (rai_qt::status_types::locked);
	}
	else
	{
		this->wallet.active_status.erase (rai_qt::status_types::locked);
	}
	if (vulnerable)
	{
		this->wallet.active_status.insert (rai_qt::status_types::vulnerable);
	}
	else
	{
		this->wallet.active_status.erase (rai_qt::status_types::vulnerable);
	}
}

bool rai_qt::settings::isLocked ()
{
	rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, false);
	return !this->wallet.wallet_m->store.valid_password (transaction);
}

void rai_qt::settings::changePassword (QString password)
{
	rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, true);
	if (this->wallet.wallet_m->store.valid_password (transaction))
	{
		this->wallet.wallet_m->store.rekey (transaction, password.toStdString ());
		Q_EMIT changePasswordSuccess ();
		update_locked (false, false);
	}
	else
	{
		Q_EMIT changePasswordFailure ("Wallet is locked, unlock it");
	}
}

QString rai_qt::settings::getRepresentative ()
{
	refresh_representative ();
	return m_representative;
}

void rai_qt::settings::changeRepresentative (QString address)
{
	rai::account representative_l;
	if (!representative_l.decode_account (address.toStdString ()))
	{
		rai::transaction transaction (this->wallet.wallet_m->store.environment, nullptr, false);
		if (this->wallet.wallet_m->store.valid_password (transaction))
		{
			{
				rai::transaction transaction_l (this->wallet.wallet_m->store.environment, nullptr, true);
				this->wallet.wallet_m->store.representative_set (transaction_l, representative_l);
			}
			auto block (this->wallet.wallet_m->change_sync (this->wallet.account, representative_l));
			Q_EMIT changeRepresentativeSuccess ();
			refresh_representative ();
		}
		else
		{
			Q_EMIT changeRepresentativeFailure ("Wallet is locked, unlock it");
		}
	}
	else
	{
		Q_EMIT changeRepresentativeFailure ("Invalid account");
	}
}

void rai_qt::settings::unlock (QString password)
{
	if (!this->wallet.wallet_m->enter_password (password.toStdString ()))
	{
		Q_EMIT unlockSuccess ();
	}
	else
	{
		Q_EMIT unlockFailure ("Invalid password");
	}
	Q_EMIT lockedChanged (isLocked ());
}

void rai_qt::settings::lock ()
{
	rai::raw_key empty;
	empty.data.clear ();
	this->wallet.wallet_m->store.password.value_set (empty);
	update_locked (true, true);
	Q_EMIT lockedChanged (isLocked ());
}

rai_qt::advanced_actions::advanced_actions (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
show_ledger (new QPushButton ("Ledger")),
show_peers (new QPushButton ("Peers")),
search_for_receivables (new QPushButton ("Search for receivables")),
bootstrap (new QPushButton ("Initiate bootstrap")),
wallet_refresh (new QPushButton ("Refresh Wallet")),
create_block (new QPushButton ("Create Block")),
enter_block (new QPushButton ("Enter Block")),
block_viewer (new QPushButton ("Block Viewer")),
account_viewer (new QPushButton ("Account Viewer")),
back (new QPushButton ("Back")),
ledger_window (new QWidget),
ledger_layout (new QVBoxLayout),
ledger_model (new QStandardItemModel),
ledger_view (new QTableView),
ledger_refresh (new QPushButton ("Refresh")),
ledger_back (new QPushButton ("Back")),
peers_window (new QWidget),
peers_layout (new QVBoxLayout),
peers_model (new QStandardItemModel),
peers_view (new QTableView),
bootstrap_label (new QLabel ("IPV6:port \"::ffff:192.168.0.1:7075\"")),
bootstrap_line (new QLineEdit),
peers_bootstrap (new QPushButton ("Initiate Bootstrap")),
peers_refresh (new QPushButton ("Refresh")),
peers_back (new QPushButton ("Back")),
wallet (wallet_a)
{
	ledger_model->setHorizontalHeaderItem (0, new QStandardItem ("Account"));
	ledger_model->setHorizontalHeaderItem (1, new QStandardItem ("Balance"));
	ledger_model->setHorizontalHeaderItem (2, new QStandardItem ("Block"));
	ledger_view->setModel (ledger_model);
	ledger_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
	ledger_view->verticalHeader ()->hide ();
	ledger_view->horizontalHeader ()->setStretchLastSection (true);
	ledger_layout->addWidget (ledger_view);
	ledger_layout->addWidget (ledger_refresh);
	ledger_layout->addWidget (ledger_back);
	ledger_layout->setContentsMargins (0, 0, 0, 0);
	ledger_window->setLayout (ledger_layout);

	peers_model->setHorizontalHeaderItem (0, new QStandardItem ("IPv6 address:port"));
	peers_model->setHorizontalHeaderItem (1, new QStandardItem ("Net version"));
	peers_view->setEditTriggers (QAbstractItemView::NoEditTriggers);
	peers_view->verticalHeader ()->hide ();
	peers_view->setModel (peers_model);
	peers_view->setColumnWidth (0, 220);
	peers_view->setSortingEnabled (true);
	peers_view->horizontalHeader ()->setStretchLastSection (true);
	peers_layout->addWidget (peers_view);
	peers_layout->addWidget (bootstrap_label);
	peers_layout->addWidget (bootstrap_line);
	peers_layout->addWidget (peers_bootstrap);
	peers_layout->addWidget (peers_refresh);
	peers_layout->addWidget (peers_back);
	peers_layout->setContentsMargins (0, 0, 0, 0);
	peers_window->setLayout (peers_layout);

	layout->addWidget (show_ledger);
	layout->addWidget (show_peers);
	layout->addWidget (search_for_receivables);
	layout->addWidget (bootstrap);
	layout->addWidget (wallet_refresh);
	layout->addWidget (create_block);
	layout->addWidget (enter_block);
	layout->addWidget (block_viewer);
	layout->addWidget (account_viewer);
	layout->addStretch ();
	layout->addWidget (back);
	window->setLayout (layout);

	QObject::connect (show_peers, &QPushButton::released, [this]() {
		refresh_peers ();
		this->wallet.push_main_stack (peers_window);
	});
	QObject::connect (show_ledger, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (ledger_window);
	});
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (peers_back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (peers_bootstrap, &QPushButton::released, [this]() {
		rai::endpoint endpoint;
		auto error (rai::parse_endpoint (bootstrap_line->text ().toStdString (), endpoint));
		if (!error)
		{
			show_line_ok (*bootstrap_line);
			bootstrap_line->clear ();
			this->wallet.node.bootstrap_initiator.bootstrap (endpoint);
		}
		else
		{
			show_line_error (*bootstrap_line);
		}
	});
	QObject::connect (peers_refresh, &QPushButton::released, [this]() {
		refresh_peers ();
	});
	QObject::connect (ledger_refresh, &QPushButton::released, [this]() {
		refresh_ledger ();
	});
	QObject::connect (ledger_back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	QObject::connect (search_for_receivables, &QPushButton::released, [this]() {
		this->wallet.wallet_m->search_pending ();
	});
	QObject::connect (bootstrap, &QPushButton::released, [this]() {
		this->wallet.node.bootstrap_initiator.bootstrap ();
	});
	QObject::connect (create_block, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (this->wallet.block_creation.window);
	});
	QObject::connect (enter_block, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (this->wallet.block_entry.window);
	});
	QObject::connect (block_viewer, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (this->wallet.block_viewer.window);
	});
	QObject::connect (account_viewer, &QPushButton::released, [this]() {
		this->wallet.push_main_stack (this->wallet.account_viewer.window);
	});
	bootstrap->setToolTip ("Multi-connection bootstrap to random peers");
	search_for_receivables->setToolTip ("Search for pending blocks");
	create_block->setToolTip ("Create block in JSON format");
	enter_block->setToolTip ("Enter block in JSON format");
}

void rai_qt::advanced_actions::refresh_peers ()
{
	peers_model->removeRows (0, peers_model->rowCount ());
	auto list (wallet.node.peers.list_version ());
	for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
	{
		std::stringstream endpoint;
		endpoint << i->first.address ().to_string ();
		endpoint << ':';
		endpoint << i->first.port ();
		QString qendpoint (endpoint.str ().c_str ());
		QList<QStandardItem *> items;
		items.push_back (new QStandardItem (qendpoint));
		items.push_back (new QStandardItem (QString (std::to_string (i->second).c_str ())));
		peers_model->appendRow (items);
	}
}

void rai_qt::advanced_actions::refresh_ledger ()
{
	ledger_model->removeRows (0, ledger_model->rowCount ());
	rai::transaction transaction (wallet.node.store.environment, nullptr, false);
	for (auto i (wallet.node.ledger.store.latest_begin (transaction)), j (wallet.node.ledger.store.latest_end ()); i != j; ++i)
	{
		QList<QStandardItem *> items;
		items.push_back (new QStandardItem (QString (rai::block_hash (i->first.uint256 ()).to_account ().c_str ())));
		rai::account_info info (i->second);
		std::string balance;
		rai::amount (info.balance.number () / wallet.rendering_ratio).encode_dec (balance);
		items.push_back (new QStandardItem (QString (balance.c_str ())));
		std::string block_hash;
		info.head.encode_hex (block_hash);
		items.push_back (new QStandardItem (QString (block_hash.c_str ())));
		ledger_model->appendRow (items);
	}
}

rai_qt::block_entry::block_entry (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
block (new QPlainTextEdit),
status (new QLabel),
process (new QPushButton ("Process")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	layout->addWidget (block);
	layout->addWidget (status);
	layout->addWidget (process);
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (process, &QPushButton::released, [this]() {
		auto string (block->toPlainText ().toStdString ());
		try
		{
			boost::property_tree::ptree tree;
			std::stringstream istream (string);
			boost::property_tree::read_json (istream, tree);
			auto block_l (rai::deserialize_block_json (tree));
			if (block_l != nullptr)
			{
				show_label_ok (*status);
				this->status->setText ("");
				this->wallet.node.process_active (std::move (block_l));
			}
			else
			{
				show_label_error (*status);
				this->status->setText ("Unable to parse block");
			}
		}
		catch (std::runtime_error const &)
		{
			show_label_error (*status);
			this->status->setText ("Unable to parse block");
		}
	});
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
}

rai_qt::block_creation::block_creation (rai_qt::wallet & wallet_a) :
window (new QWidget),
layout (new QVBoxLayout),
group (new QButtonGroup),
button_layout (new QHBoxLayout),
send (new QRadioButton ("Send")),
receive (new QRadioButton ("Receive")),
change (new QRadioButton ("Change")),
open (new QRadioButton ("Open")),
account_label (new QLabel ("Account:")),
account (new QLineEdit),
source_label (new QLabel ("Source:")),
source (new QLineEdit),
amount_label (new QLabel ("Amount:")),
amount (new QLineEdit),
destination_label (new QLabel ("Destination:")),
destination (new QLineEdit),
representative_label (new QLabel ("Representative:")),
representative (new QLineEdit),
block (new QPlainTextEdit),
status (new QLabel),
create (new QPushButton ("Create")),
back (new QPushButton ("Back")),
wallet (wallet_a)
{
	group->addButton (send);
	group->addButton (receive);
	group->addButton (change);
	group->addButton (open);
	group->setId (send, 0);
	group->setId (receive, 1);
	group->setId (change, 2);
	group->setId (open, 3);

	button_layout->addWidget (send);
	button_layout->addWidget (receive);
	button_layout->addWidget (open);
	button_layout->addWidget (change);

	layout->addLayout (button_layout);
	layout->addWidget (account_label);
	layout->addWidget (account);
	layout->addWidget (source_label);
	layout->addWidget (source);
	layout->addWidget (amount_label);
	layout->addWidget (amount);
	layout->addWidget (destination_label);
	layout->addWidget (destination);
	layout->addWidget (representative_label);
	layout->addWidget (representative);
	layout->addWidget (block);
	layout->addWidget (status);
	layout->addWidget (create);
	layout->addWidget (back);
	window->setLayout (layout);
	QObject::connect (send, &QRadioButton::toggled, [this]() {
		if (send->isChecked ())
		{
			deactivate_all ();
			activate_send ();
		}
	});
	QObject::connect (receive, &QRadioButton::toggled, [this]() {
		if (receive->isChecked ())
		{
			deactivate_all ();
			activate_receive ();
		}
	});
	QObject::connect (open, &QRadioButton::toggled, [this]() {
		if (open->isChecked ())
		{
			deactivate_all ();
			activate_open ();
		}
	});
	QObject::connect (change, &QRadioButton::toggled, [this]() {
		if (change->isChecked ())
		{
			deactivate_all ();
			activate_change ();
		}
	});
	QObject::connect (create, &QPushButton::released, [this]() {
		switch (group->checkedId ())
		{
			case 0:
				create_send ();
				break;
			case 1:
				create_receive ();
				break;
			case 2:
				create_change ();
				break;
			case 3:
				create_open ();
				break;
			default:
				assert (false);
				break;
		}
	});
	QObject::connect (back, &QPushButton::released, [this]() {
		this->wallet.pop_main_stack ();
	});
	send->click ();
}

void rai_qt::block_creation::deactivate_all ()
{
	account_label->hide ();
	account->hide ();
	source_label->hide ();
	source->hide ();
	amount_label->hide ();
	amount->hide ();
	destination_label->hide ();
	destination->hide ();
	representative_label->hide ();
	representative->hide ();
}

void rai_qt::block_creation::activate_send ()
{
	account_label->show ();
	account->show ();
	amount_label->show ();
	amount->show ();
	destination_label->show ();
	destination->show ();
}

void rai_qt::block_creation::activate_receive ()
{
	source_label->show ();
	source->show ();
}

void rai_qt::block_creation::activate_open ()
{
	source_label->show ();
	source->show ();
	representative_label->show ();
	representative->show ();
}

void rai_qt::block_creation::activate_change ()
{
	account_label->show ();
	account->show ();
	representative_label->show ();
	representative->show ();
}

void rai_qt::block_creation::create_send ()
{
	rai::account account_l;
	auto error (account_l.decode_account (account->text ().toStdString ()));
	if (!error)
	{
		rai::amount amount_l;
		error = amount_l.decode_dec (amount->text ().toStdString ());
		if (!error)
		{
			rai::account destination_l;
			error = destination_l.decode_account (destination->text ().toStdString ());
			if (!error)
			{
				rai::transaction transaction (wallet.node.store.environment, nullptr, false);
				rai::raw_key key;
				if (!wallet.wallet_m->store.fetch (transaction, account_l, key))
				{
					auto balance (wallet.node.ledger.account_balance (transaction, account_l));
					if (amount_l.number () <= balance)
					{
						rai::account_info info;
						auto error (wallet.node.store.account_get (transaction, account_l, info));
						assert (!error);
						rai::send_block send (info.head, destination_l, balance - amount_l.number (), key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
						std::string block_l;
						send.serialize_json (block_l);
						block->setPlainText (QString (block_l.c_str ()));
						show_label_ok (*status);
						status->setText ("Created block");
					}
					else
					{
						show_label_error (*status);
						status->setText ("Insufficient balance");
					}
				}
				else
				{
					show_label_error (*status);
					status->setText ("Account is not in wallet");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText ("Unable to decode destination");
			}
		}
		else
		{
			show_label_error (*status);
			status->setText ("Unable to decode amount");
		}
	}
	else
	{
		show_label_error (*status);
		status->setText ("Unable to decode account");
	}
}

void rai_qt::block_creation::create_receive ()
{
	rai::block_hash source_l;
	auto error (source_l.decode_hex (source->text ().toStdString ()));
	if (!error)
	{
		rai::transaction transaction (wallet.node.store.environment, nullptr, false);
		auto block_l (wallet.node.store.block_get (transaction, source_l));
		if (block_l != nullptr)
		{
			auto send_block (dynamic_cast<rai::send_block *> (block_l.get ()));
			if (send_block != nullptr)
			{
				rai::pending_key pending_key (send_block->hashables.destination, source_l);
				rai::pending_info pending;
				if (!wallet.node.store.pending_get (transaction, pending_key, pending))
				{
					rai::account_info info;
					auto error (wallet.node.store.account_get (transaction, pending_key.account, info));
					if (!error)
					{
						rai::raw_key key;
						auto error (wallet.wallet_m->store.fetch (transaction, pending_key.account, key));
						if (!error)
						{
							rai::receive_block receive (info.head, source_l, key, pending_key.account, wallet.wallet_m->work_fetch (transaction, pending_key.account, info.head));
							std::string block_l;
							receive.serialize_json (block_l);
							block->setPlainText (QString (block_l.c_str ()));
							show_label_ok (*status);
							status->setText ("Created block");
						}
						else
						{
							show_label_error (*status);
							status->setText ("Account is not in wallet");
						}
					}
					else
					{
						show_label_error (*status);
						status->setText ("Account not yet open");
					}
				}
				else
				{
					show_label_error (*status);
					status->setText ("Source block is not pending to receive");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText ("Source is not a send block");
			}
		}
		else
		{
			show_label_error (*status);
			status->setText ("Source block not found");
		}
	}
	else
	{
		show_label_error (*status);
		status->setText ("Unable to decode source");
	}
}

void rai_qt::block_creation::create_change ()
{
	rai::account account_l;
	auto error (account_l.decode_account (account->text ().toStdString ()));
	if (!error)
	{
		rai::account representative_l;
		error = representative_l.decode_account (representative->text ().toStdString ());
		if (!error)
		{
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
			rai::account_info info;
			auto error (wallet.node.store.account_get (transaction, account_l, info));
			if (!error)
			{
				rai::raw_key key;
				auto error (wallet.wallet_m->store.fetch (transaction, account_l, key));
				if (!error)
				{
					rai::change_block change (info.head, representative_l, key, account_l, wallet.wallet_m->work_fetch (transaction, account_l, info.head));
					std::string block_l;
					change.serialize_json (block_l);
					block->setPlainText (QString (block_l.c_str ()));
					show_label_ok (*status);
					status->setText ("Created block");
				}
				else
				{
					show_label_error (*status);
					status->setText ("Account is not in wallet");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText ("Account not yet open");
			}
		}
		else
		{
			show_label_error (*status);
			status->setText ("Unable to decode representative");
		}
	}
	else
	{
		show_label_error (*status);
		status->setText ("Unable to decode account");
	}
}

void rai_qt::block_creation::create_open ()
{
	rai::block_hash source_l;
	auto error (source_l.decode_hex (source->text ().toStdString ()));
	if (!error)
	{
		rai::account representative_l;
		error = representative_l.decode_account (representative->text ().toStdString ());
		if (!error)
		{
			rai::transaction transaction (wallet.node.store.environment, nullptr, false);
			auto block_l (wallet.node.store.block_get (transaction, source_l));
			if (block_l != nullptr)
			{
				auto send_block (dynamic_cast<rai::send_block *> (block_l.get ()));
				if (send_block != nullptr)
				{
					rai::pending_key pending_key (send_block->hashables.destination, source_l);
					rai::pending_info pending;
					if (!wallet.node.store.pending_get (transaction, pending_key, pending))
					{
						rai::account_info info;
						auto error (wallet.node.store.account_get (transaction, pending_key.account, info));
						if (error)
						{
							rai::raw_key key;
							auto error (wallet.wallet_m->store.fetch (transaction, pending_key.account, key));
							if (!error)
							{
								rai::open_block open (source_l, representative_l, pending_key.account, key, pending_key.account, wallet.wallet_m->work_fetch (transaction, pending_key.account, pending_key.account));
								std::string block_l;
								open.serialize_json (block_l);
								block->setPlainText (QString (block_l.c_str ()));
								show_label_ok (*status);
								status->setText ("Created block");
							}
							else
							{
								show_label_error (*status);
								status->setText ("Account is not in wallet");
							}
						}
						else
						{
							show_label_error (*status);
							status->setText ("Account already open");
						}
					}
					else
					{
						show_label_error (*status);
						status->setText ("Source block is not pending to receive");
					}
				}
				else
				{
					show_label_error (*status);
					status->setText ("Source is not a send block");
				}
			}
			else
			{
				show_label_error (*status);
				status->setText ("Source block not found");
			}
		}
		else
		{
			show_label_error (*status);
			status->setText ("Unable to decode representative");
		}
	}
	else
	{
		show_label_error (*status);
		status->setText ("Unable to decode source");
	}
}
