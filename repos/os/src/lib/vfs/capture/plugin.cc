/*
 * \brief  VFS capture plugin
 * \author Christian Prochaska
 * \date   2021-09-08
 */

/*
 * Copyright (C) 2021-2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <capture_session/connection.h>
#include <vfs/single_file_system.h>
#include <vfs/dir_file_system.h>
#include <vfs/readonly_value_file_system.h>
#include <util/xml_generator.h>


namespace Vfs_capture
{
	using namespace Genode;
	using namespace Vfs;

	using Name = String<64>;

	struct Data_file_system;
	struct Local_factory;
	struct File_system;
};


class Vfs_capture::Data_file_system : public Single_file_system
{
	private:

		Name const &_name;

		using Label = Genode::String<64>;
		Label const &_label;

		Genode::Env &_env;

		Capture::Area const _capture_area { 640, 480 };
		Constructible<Capture::Connection> _capture { };
		Constructible<Attached_dataspace>  _capture_ds { };

		unsigned int _open_count { 0 };

		struct Capture_vfs_handle : Single_vfs_handle
		{
			Constructible<Capture::Connection> &_capture;
			Constructible<Attached_dataspace>  &_capture_ds;

			bool notifying = false;
			bool blocked   = false;

			Capture_vfs_handle(Constructible<Capture::Connection> &capture,
			                   Constructible<Attached_dataspace>  &capture_ds,
			                   Directory_service  &ds,
			                   File_io_service    &fs,
			                   Genode::Allocator  &alloc,
			                   int                 flags)
			:
				Single_vfs_handle(ds, fs, alloc, flags),
				_capture(capture), _capture_ds(capture_ds)
			{ }

			bool read_ready()  const override { return true; }
			bool write_ready() const override { return true; }

			Read_result read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				_capture->capture_at(Point(0, 0));

				size_t const len = min(dst.num_bytes, _capture_ds->size());

				Genode::memcpy(dst.start, _capture_ds->local_addr<char>(), len);

				out_count = len;

				return READ_OK;
			}

			Write_result write(Const_byte_range_ptr const &, size_t &) override
			{
				return WRITE_ERR_IO;
			}
		};

		using Registered_handle = Genode::Registered<Capture_vfs_handle>;
		using Handle_registry   = Genode::Registry<Registered_handle>;

		Handle_registry _handle_registry { };

	public:

		Data_file_system(Name        const &name,
		                 Label       const &label,
		                 Genode::Env       &env)
		:
			Single_file_system(Node_type::TRANSACTIONAL_FILE, name.string(),
			                   Node_rwx::rw(), Genode::Xml_node("<data/>")),
			_name(name), _label(label), _env(env)
		{ }

		static const char *name()   { return "data"; }
		char const *type() override { return "data"; }

		Open_result open(char const  *path, unsigned flags,
		                 Vfs_handle **out_handle,
		                 Allocator   &alloc) override
		{
			if (!_single_file(path))
				return OPEN_ERR_UNACCESSIBLE;

			if (_open_count == 0) {
				try {
					_capture.construct(_env, _label.string());
				} catch (Genode::Service_denied) {
					return OPEN_ERR_UNACCESSIBLE;
				}
				_capture->buffer({ .px       = _capture_area,
				                   .mm       = { },
				                   .viewport = { { }, _capture_area } });
				_capture_ds.construct(_env.rm(), _capture->dataspace());
			}

			try {
				*out_handle = new (alloc)
					Registered_handle(_handle_registry,
					                  _capture, _capture_ds,
					                  *this, *this, alloc, flags);
				return OPEN_OK;
			}
			catch (Genode::Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }
		}


		void close(Vfs_handle *handle) override
		{
			_open_count--;

			if (_open_count == 0) {
				_capture_ds.destruct();
				_capture.destruct();
			}

			Single_file_system::close(handle);
		}


		/********************************
		 ** File I/O service interface **
		 ********************************/

		bool notify_read_ready(Vfs_handle *vfs_handle) override
		{
			Capture_vfs_handle *handle =
				static_cast<Capture_vfs_handle*>(vfs_handle);
			if (!handle)
				return false;

			handle->notifying = true;
			return true;
		}

		Ftruncate_result ftruncate(Vfs_handle *, file_size) override
		{
			return FTRUNCATE_OK;
		}
};


struct Vfs_capture::Local_factory : File_system_factory
{
	using Label = Genode::String<64>;
	Label const _label;

	Name const _name;

	Genode::Env &_env;

	Data_file_system _data_fs { _name, _label, _env };

	static Name name(Xml_node const &config)
	{
		return config.attribute_value("name", Name("capture"));
	}

	Local_factory(Vfs::Env &env, Xml_node const &config)
	:
		_label(config.attribute_value("label", Label(""))),
		_name(name(config)),
		_env(env.env())
	{ }

	Vfs::File_system *create(Vfs::Env&, Xml_node const &node) override
	{
		return node.has_type("data") ? &_data_fs : nullptr;
	}
};


class Vfs_capture::File_system : private Local_factory,
                                 public  Vfs::Dir_file_system
{
	private:

		using Name = Vfs_capture::Name;

		using Config = String<200>;
		static Config _config(Name const &name)
		{
			char buf[Config::capacity()] { };

			/*
			 * By not using the node type "dir", we operate the
			 * 'Dir_file_system' in root mode, allowing multiple sibling nodes
			 * to be present at the mount point.
			 */
			Genode::Xml_generator::generate({ buf, sizeof(buf) }, "compound",
				[&] (Genode::Xml_generator &xml) {
					xml.node("data", [&] { xml.attribute("name", name); });
					xml.node("dir",  [&] { xml.attribute("name", Name(".", name)); });
			}).with_error([] (Genode::Buffer_error) {
				Genode::warning("VFS-capture compound exceeds maximum buffer size");
			});

			return Config(Genode::Cstring(buf));
		}

	public:

		File_system(Vfs::Env &vfs_env, Genode::Xml_node const &node)
		:
			Local_factory(vfs_env, node),
			Vfs::Dir_file_system(vfs_env,
			                     Xml_node(_config(Local_factory::name(node)).string()),
			                     *this)
		{ }

		static const char *name() { return "capture"; }

		char const *type() override { return name(); }
};


extern "C" Vfs::File_system_factory *vfs_file_system_factory(void)
{
	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Genode::Xml_node const &node) override
		{
			return new (env.alloc()) Vfs_capture::File_system(env, node);
		}
	};

	static Factory f;
	return &f;
}
