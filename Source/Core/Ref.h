#pragma once

template <typename T>
class Ref
{
public:
	using InterfaceType = T;

protected:
	InterfaceType* ptr_;
	template<class U> friend class Ref;

	void InternalAddRef() const noexcept
	{
		if (ptr_ != nullptr)
		{
			ptr_->AddRef();
		}
	}

	void InternalRelease() noexcept
	{
		T* temp = ptr_;
		if (temp != nullptr)
		{
			ptr_ = nullptr;
			temp->Release();
		}
	}

public:

	Ref() noexcept
		: ptr_(nullptr)
	{
	}

	Ref(std::nullptr_t) noexcept
		: ptr_(nullptr)
	{
	}

	template<class U>
	Ref(U* other) noexcept
		: ptr_(other)
	{
		InternalAddRef();
	}

	Ref(const Ref& other) noexcept
		: ptr_(other.ptr_)
	{
		InternalAddRef();
	}

	// copy ctor that allows to instanatiate class when U* is convertible to T*
	template<class U>
	Ref(const Ref<U>& other, typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
		: ptr_(other.ptr_)

	{
		InternalAddRef();
	}

	Ref(Ref&& other) noexcept
		: ptr_(nullptr)
	{
		if (this != reinterpret_cast<Ref*>(&reinterpret_cast<unsigned char&>(other)))
		{
			Swap(other);
		}
	}

	// Move ctor that allows instantiation of a class when U* is convertible to T*
	template<class U>
	Ref(Ref<U>&& other, typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
		: 	ptr_(other.ptr_)
	{
		other.ptr_ = nullptr;
	}

	~Ref() noexcept
	{
		InternalRelease();
	}

	Ref& operator=(std::nullptr_t) noexcept
	{
		InternalRelease();
		return *this;
	}

	Ref& operator=(T* other) noexcept
	{
		if (ptr_ != other)
		{
			Ref(other).Swap(*this);
		}
		return *this;
	}

	template <typename U>
	Ref& operator=(U* other) noexcept
	{
		Ref(other).Swap(*this);
		return *this;
	}

	Ref& operator=(const Ref& other) noexcept
	{
		if (ptr_ != other.ptr_)
		{
			Ref(other).Swap(*this);
		}
		return *this;
	}

	template<class U>
	Ref& operator=(const Ref<U>& other) noexcept
	{
		Ref(other).Swap(*this);
		return *this;
	}

	Ref& operator=(Ref&& other) noexcept
	{
		Ref(static_cast<Ref&&>(other)).Swap(*this);
		return *this;
	}

	template<class U>
	Ref& operator=(Ref<U>&& other) noexcept
	{
		Ref(static_cast<Ref<U>&&>(other)).Swap(*this);
		return *this;
	}

	void Swap(Ref&& r) noexcept
	{
		T* tmp = ptr_;
		ptr_ = r.ptr_;
		r.ptr_ = tmp;
	}

	void Swap(Ref& r) noexcept
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

	template<typename K>
	bool As(Ref<K>* pTarget)
	{
		static_assert(std::is_base_of_v<IUnknown, K> && std::is_base_of_v<IUnknown, T>, "Type must inherit from IUnknown to support As()");
		return SUCCEEDED(ptr_->QueryInterface(IID_PPV_ARGS(pTarget->ReleaseAndGetAddressOf())));
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

	void Reset()
	{
		InternalRelease();
	}
};


template<typename T>
class RefCounted
{
public:
	uint32 AddRef()
	{
		return m_RefCount.fetch_add(1);
	}

	uint32 Release()
	{
		uint32 count_prev = m_RefCount.fetch_sub(1);
		gAssert(count_prev >= 1);
		if (count_prev == 1)
			Destroy();
		return count_prev;
	}

	uint32 GetNumRefs() const { return m_RefCount; }

private:
	void Destroy() { delete static_cast<T*>(this); }

	std::atomic<uint32> m_RefCount = 0;
};
