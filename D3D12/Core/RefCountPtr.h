#pragma once

template <typename T>
class RefCountPtr
{
public:
	using InterfaceType = T;

protected:
	InterfaceType* ptr_;
	template<class U> friend class RefCountPtr;

	void InternalAddRef() const noexcept
	{
		if (ptr_ != nullptr)
		{
			ptr_->AddRef();
		}
	}

	unsigned long InternalRelease() noexcept
	{
		unsigned long ref = 0;
		T* temp = ptr_;

		if (temp != nullptr)
		{
			ptr_ = nullptr;
			ref = temp->Release();
		}

		return ref;
	}

public:

	RefCountPtr() noexcept
		: ptr_(nullptr)
	{
	}

	RefCountPtr(std::nullptr_t) noexcept
		: ptr_(nullptr)
	{
	}

	template<class U>
	RefCountPtr(U* other) noexcept
		: ptr_(other)
	{
		InternalAddRef();
	}

	RefCountPtr(const RefCountPtr& other) noexcept
		: ptr_(other.ptr_)
	{
		InternalAddRef();
	}

	// copy ctor that allows to instanatiate class when U* is convertible to T*
	template<class U>
	RefCountPtr(const RefCountPtr<U>& other, typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
		: ptr_(other.ptr_)

	{
		InternalAddRef();
	}

	RefCountPtr(RefCountPtr&& other) noexcept
		: ptr_(nullptr)
	{
		if (this != reinterpret_cast<RefCountPtr*>(&reinterpret_cast<unsigned char&>(other)))
		{
			Swap(other);
		}
	}

	// Move ctor that allows instantiation of a class when U* is convertible to T*
	template<class U>
	RefCountPtr(RefCountPtr<U>&& other, typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
		: 	ptr_(other.ptr_)
	{
		other.ptr_ = nullptr;
	}

	~RefCountPtr() noexcept
	{
		InternalRelease();
	}

	RefCountPtr& operator=(std::nullptr_t) noexcept
	{
		InternalRelease();
		return *this;
	}

	RefCountPtr& operator=(T* other) noexcept
	{
		if (ptr_ != other)
		{
			RefCountPtr(other).Swap(*this);
		}
		return *this;
	}

	template <typename U>
	RefCountPtr& operator=(U* other) noexcept
	{
		RefCountPtr(other).Swap(*this);
		return *this;
	}

	RefCountPtr& operator=(const RefCountPtr& other) noexcept
	{
		if (ptr_ != other.ptr_)
		{
			RefCountPtr(other).Swap(*this);
		}
		return *this;
	}

	template<class U>
	RefCountPtr& operator=(const RefCountPtr<U>& other) noexcept
	{
		RefCountPtr(other).Swap(*this);
		return *this;
	}

	RefCountPtr& operator=(RefCountPtr&& other) noexcept
	{
		RefCountPtr(static_cast<RefCountPtr&&>(other)).Swap(*this);
		return *this;
	}

	template<class U>
	RefCountPtr& operator=(RefCountPtr<U>&& other) noexcept
	{
		RefCountPtr(static_cast<RefCountPtr<U>&&>(other)).Swap(*this);
		return *this;
	}

	void Swap(RefCountPtr&& r) noexcept
	{
		T* tmp = ptr_;
		ptr_ = r.ptr_;
		r.ptr_ = tmp;
	}

	void Swap(RefCountPtr& r) noexcept
	{
		T* tmp = ptr_;
		ptr_ = r.ptr_;
		r.ptr_ = tmp;
	}

	[[nodiscard]] T* Get() const noexcept
	{
		return ptr_;
	}

	operator T* () const
	{
		return ptr_;
	}

	InterfaceType* operator->() const noexcept
	{
		return ptr_;
	}

	T** operator&()
	{
		return &ptr_;
	}

	[[nodiscard]] T* const* GetAddressOf() const noexcept
	{
		return &ptr_;
	}

	[[nodiscard]] T** GetAddressOf() noexcept
	{
		return &ptr_;
	}

	[[nodiscard]] T** ReleaseAndGetAddressOf() noexcept
	{
		InternalRelease();
		return &ptr_;
	}

	T* Detach() noexcept
	{
		T* ptr = ptr_;
		ptr_ = nullptr;
		return ptr;
	}

	// Set the pointer while keeping the object's reference count unchanged
	void Attach(InterfaceType* other)
	{
		if (ptr_ != nullptr)
		{
			auto ref = ptr_->Release();
			(void)ref;

			// Attaching to the same object only works if duplicate references are being coalesced. Otherwise
			// re-attaching will cause the pointer to be released and may cause a crash on a subsequent dereference.
			assert(ref != 0 || ptr_ != other);
		}

		ptr_ = other;
	}

	// Create a wrapper around a raw object while keeping the object's reference count unchanged
	static RefCountPtr<T> Create(T* other)
	{
		RefCountPtr<T> Ptr;
		Ptr.Attach(other);
		return Ptr;
	}

	unsigned long Reset()
	{
		return InternalRelease();
	}
};
